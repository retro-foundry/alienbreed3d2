#include "dxr_streamline.h"

#include "dxr_debug.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss_d.h>
#include <sl_helpers.h>
#include <sl_matrix_helpers.h>
#include <sl_security.h>

#include <array>
#include <cmath>
#include <cwchar>
#include <cstring>
#include <sstream>
#include <vector>

namespace ab3d2::dxr {

namespace {

constexpr float camera_near_plane = reconstruction::scene_near_plane;
constexpr float camera_far_plane = reconstruction::scene_far_plane;
constexpr float invalid_motion_value = 65504.0f;
const sl::ViewportHandle rr_viewport{1u};

std::string result_error(const char *operation, sl::Result result)
{
    std::string message = operation ? operation : "Streamline operation";
    message += " failed: ";
    message += sl::getResultAsStr(result);
    return message;
}

bool executable_directory(std::filesystem::path &directory, std::string &error)
{
    std::wstring executable_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
    if (length == 0 || length >= executable_path.size()) {
        error = "Streamline could not resolve the executable's full path";
        return false;
    }
    executable_path.resize(length);
    directory = std::filesystem::path(executable_path).parent_path();
    if (directory.empty() || !directory.is_absolute()) {
        error = "Streamline executable directory is not absolute";
        return false;
    }
    return true;
}

bool verify_nvidia_authenticode(const std::filesystem::path &path)
{
    WINTRUST_FILE_INFO file_info = {};
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = path.c_str();
    WINTRUST_DATA trust_data = {};
    trust_data.cbStruct = sizeof(trust_data);
    trust_data.dwUIChoice = WTD_UI_NONE;
    trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust_data.dwUnionChoice = WTD_CHOICE_FILE;
    trust_data.pFile = &file_info;
    trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
    trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG trust_status = WinVerifyTrust(nullptr, &policy, &trust_data);
    trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(nullptr, &policy, &trust_data);
    if (trust_status != ERROR_SUCCESS) {
        return false;
    }

    DWORD encoding = 0;
    DWORD content_type = 0;
    DWORD format_type = 0;
    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    if (!CryptQueryObject(
            CERT_QUERY_OBJECT_FILE, path.c_str(),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &content_type,
            &format_type, &store, &message, nullptr)) {
        return false;
    }
    DWORD signer_size = 0;
    bool valid = CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr,
                                  &signer_size) != FALSE &&
        signer_size >= sizeof(CMSG_SIGNER_INFO);
    std::vector<uint8_t> signer_bytes(valid ? signer_size : 0u);
    if (valid) {
        valid = CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0,
                                 signer_bytes.data(), &signer_size) != FALSE;
    }
    PCCERT_CONTEXT certificate = nullptr;
    if (valid) {
        const auto *signer = reinterpret_cast<const CMSG_SIGNER_INFO *>(
            signer_bytes.data());
        CERT_INFO certificate_info = {};
        certificate_info.Issuer = signer->Issuer;
        certificate_info.SerialNumber = signer->SerialNumber;
        certificate = CertFindCertificateInStore(
            store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
            CERT_FIND_SUBJECT_CERT, &certificate_info, nullptr);
        valid = certificate != nullptr;
    }
    if (valid) {
        wchar_t publisher[256] = {};
        const DWORD length = CertGetNameStringW(
            certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, publisher,
            static_cast<DWORD>(std::size(publisher)));
        valid = length > 1 && std::wcscmp(publisher, L"NVIDIA Corporation") == 0;
    }
    if (certificate) {
        CertFreeCertificateContext(certificate);
    }
    if (message) {
        CryptMsgClose(message);
    }
    if (store) {
        CertCloseStore(store, 0);
    }
    return valid;
}

void streamline_log(sl::LogType type, const char *message)
{
    (void)type;
    if (message && *message) {
        std::string text = "Streamline: ";
        text += message;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        debug_output(text);
    }
}

sl::float4x4 identity_matrix()
{
    return {
        sl::float4(1.0f, 0.0f, 0.0f, 0.0f),
        sl::float4(0.0f, 1.0f, 0.0f, 0.0f),
        sl::float4(0.0f, 0.0f, 1.0f, 0.0f),
        sl::float4(0.0f, 0.0f, 0.0f, 1.0f),
    };
}

sl::float4x4 camera_to_world(
    const reconstruction::CameraProjection &camera)
{
    return {
        sl::float4(camera.right.x, camera.right.y, camera.right.z, 0.0f),
        sl::float4(camera.up.x, camera.up.y, camera.up.z, 0.0f),
        sl::float4(camera.forward.x, camera.forward.y, camera.forward.z, 0.0f),
        sl::float4(camera.position.x, camera.position.y, camera.position.z, 1.0f),
    };
}

sl::float4x4 camera_to_clip(
    const reconstruction::CameraProjection &camera)
{
    const float y_scale = 1.0f / camera.tan_half_fov_y;
    const float x_scale = y_scale / camera.aspect;
    const float depth_scale = camera_far_plane /
        (camera_far_plane - camera_near_plane);
    return {
        sl::float4(x_scale, 0.0f, 0.0f, 0.0f),
        sl::float4(0.0f, y_scale, 0.0f, 0.0f),
        sl::float4(0.0f, 0.0f, depth_scale, 1.0f),
        sl::float4(0.0f, 0.0f, -camera_near_plane * depth_scale, 0.0f),
    };
}

sl::DLSSMode streamline_mode(DxrStreamline::Mode mode)
{
    switch (mode) {
    case DxrStreamline::Mode::quality:
        return sl::DLSSMode::eMaxQuality;
    case DxrStreamline::Mode::balanced:
        return sl::DLSSMode::eBalanced;
    case DxrStreamline::Mode::performance:
        return sl::DLSSMode::eMaxPerformance;
    case DxrStreamline::Mode::ultra_performance:
        return sl::DLSSMode::eUltraPerformance;
    case DxrStreamline::Mode::off:
        return sl::DLSSMode::eOff;
    }
    return sl::DLSSMode::eOff;
}

const char *mode_name(DxrStreamline::Mode mode)
{
    switch (mode) {
    case DxrStreamline::Mode::off:
        return "off";
    case DxrStreamline::Mode::quality:
        return "quality";
    case DxrStreamline::Mode::balanced:
        return "balanced";
    case DxrStreamline::Mode::performance:
        return "performance";
    case DxrStreamline::Mode::ultra_performance:
        return "ultra-performance";
    }
    return "unknown";
}

sl::DLSSDOptions make_options(DxrStreamline::Mode mode, UINT output_width,
                              UINT output_height,
                              const reconstruction::CameraProjection *camera)
{
    sl::DLSSDOptions options{};
    options.mode = streamline_mode(mode);
    options.outputWidth = output_width;
    options.outputHeight = output_height;
    options.sharpness = 0.0f;
    options.preExposure = 1.0f;
    options.exposureScale = 1.0f;
    options.colorBuffersHDR = sl::Boolean::eTrue;
    options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::eUnpacked;
    options.alphaUpscalingEnabled = sl::Boolean::eFalse;
    if (camera) {
        options.cameraViewToWorld = camera_to_world(*camera);
        sl::matrixOrthoNormalInvert(options.worldToCameraView,
                                   options.cameraViewToWorld);
    } else {
        options.cameraViewToWorld = identity_matrix();
        options.worldToCameraView = identity_matrix();
    }
    return options;
}

sl::Constants make_constants(
    const reconstruction::CameraProjection &current_camera,
    const reconstruction::CameraProjection &previous_camera,
    reconstruction::PixelJitter jitter, bool history_valid)
{
    const sl::float4x4 current_camera_to_world = camera_to_world(current_camera);
    const sl::float4x4 previous_camera_to_world = camera_to_world(previous_camera);
    const sl::float4x4 current_projection = camera_to_clip(current_camera);
    const sl::float4x4 previous_projection = camera_to_clip(previous_camera);

    sl::Constants constants{};
    constants.cameraViewToClip = current_projection;
    sl::matrixFullInvert(constants.clipToCameraView, current_projection);
    constants.clipToLensClip = identity_matrix();

    sl::float4x4 camera_to_previous_camera{};
    sl::calcCameraToPrevCamera(camera_to_previous_camera,
                               current_camera_to_world,
                               previous_camera_to_world);
    sl::float4x4 clip_to_previous_camera{};
    sl::matrixMul(clip_to_previous_camera, constants.clipToCameraView,
                  camera_to_previous_camera);
    sl::matrixMul(constants.clipToPrevClip, clip_to_previous_camera,
                  previous_projection);
    sl::matrixFullInvert(constants.prevClipToClip, constants.clipToPrevClip);

    constants.jitterOffset = sl::float2(jitter.x, jitter.y);
    constants.mvecScale = sl::float2(
        1.0f / static_cast<float>(current_camera.width),
        1.0f / static_cast<float>(current_camera.height));
    constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
    constants.cameraPos = sl::float3(current_camera.position.x,
                                     current_camera.position.y,
                                     current_camera.position.z);
    constants.cameraUp = sl::float3(current_camera.up.x, current_camera.up.y,
                                    current_camera.up.z);
    constants.cameraRight = sl::float3(current_camera.right.x,
                                       current_camera.right.y,
                                       current_camera.right.z);
    constants.cameraFwd = sl::float3(current_camera.forward.x,
                                     current_camera.forward.y,
                                     current_camera.forward.z);
    constants.cameraNear = camera_near_plane;
    constants.cameraFar = camera_far_plane;
    constants.cameraFOV = 2.0f * std::atan(current_camera.tan_half_fov_y);
    constants.cameraAspectRatio = current_camera.aspect;
    constants.motionVectorsInvalidValue = invalid_motion_value;
    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = history_valid ? sl::Boolean::eFalse : sl::Boolean::eTrue;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
    constants.minRelativeLinearDepthObjectSeparation = 40.0f;
    return constants;
}

bool complete_resources(const DxrStreamlineResources &resources)
{
    return resources.noisy_radiance && resources.output &&
        resources.diffuse_albedo && resources.specular_albedo &&
        resources.shading_normal && resources.linear_roughness &&
        resources.linear_depth && resources.scene_motion &&
        resources.specular_hit_distance && resources.diffuse_hit_distance;
}

}  // namespace

DxrStreamline::~DxrStreamline()
{
    std::string error;
    if (!shutdown(error) && !error.empty()) {
        debug_output("Streamline destructor shutdown failed: " + error);
    }
}

bool DxrStreamline::configure_mode(RendererRayReconstructionMode requested,
                                   std::string &error)
{
    /*
     * ab3d2.ini chooses the mode; the environment override stays because it is
     * how a single run gets measured without editing the file.
     */
    switch (requested) {
    case RENDERER_RAY_RECONSTRUCTION_QUALITY: mode_ = Mode::quality; break;
    case RENDERER_RAY_RECONSTRUCTION_BALANCED: mode_ = Mode::balanced; break;
    case RENDERER_RAY_RECONSTRUCTION_PERFORMANCE:
        mode_ = Mode::performance;
        break;
    case RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE:
        mode_ = Mode::ultra_performance;
        break;
    case RENDERER_RAY_RECONSTRUCTION_OFF: mode_ = Mode::off; break;
    case RENDERER_RAY_RECONSTRUCTION_DEFAULT: mode_ = Mode::quality; break;
    }
    char value[64] = {};
    const DWORD length = GetEnvironmentVariableA(
        "AB3D2_DXR_RR_MODE", value, static_cast<DWORD>(sizeof(value)));
    if (length >= sizeof(value)) {
        error = "AB3D2_DXR_RR_MODE exceeds 63 bytes";
        return false;
    }
    if (length == 0) {
        return true;
    }
    if (std::strcmp(value, "quality") == 0) {
        mode_ = Mode::quality;
    } else if (std::strcmp(value, "balanced") == 0) {
        mode_ = Mode::balanced;
    } else if (std::strcmp(value, "performance") == 0) {
        mode_ = Mode::performance;
    } else if (std::strcmp(value, "ultra-performance") == 0) {
        mode_ = Mode::ultra_performance;
    } else if (std::strcmp(value, "off") == 0) {
        mode_ = Mode::off;
    } else {
        error = "AB3D2_DXR_RR_MODE must be quality, balanced, performance, "
                "ultra-performance, or off";
        return false;
    }
    return true;
}

RendererRayReconstructionMode DxrStreamline::active_mode() const
{
    switch (mode_) {
    case Mode::quality: return RENDERER_RAY_RECONSTRUCTION_QUALITY;
    case Mode::balanced: return RENDERER_RAY_RECONSTRUCTION_BALANCED;
    case Mode::performance: return RENDERER_RAY_RECONSTRUCTION_PERFORMANCE;
    case Mode::ultra_performance:
        return RENDERER_RAY_RECONSTRUCTION_ULTRA_PERFORMANCE;
    case Mode::off: return RENDERER_RAY_RECONSTRUCTION_OFF;
    }
    return RENDERER_RAY_RECONSTRUCTION_DEFAULT;
}

bool DxrStreamline::verify_runtime(std::string &error)
{
    static constexpr std::array<const wchar_t *, 3> streamline_modules = {
        L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss_d.dll",
    };
    if (!executable_directory(runtime_directory_, error)) {
        return false;
    }
    for (const wchar_t *module : streamline_modules) {
        const std::filesystem::path path = runtime_directory_ / module;
        if (!std::filesystem::is_regular_file(path)) {
            error = "required signed Streamline runtime is missing: " +
                wide_to_utf8(path.c_str());
            return false;
        }
        if (!sl::security::verifyEmbeddedSignature(path.c_str())) {
            error = "NVIDIA signature verification failed for Streamline runtime: " +
                wide_to_utf8(path.c_str());
            return false;
        }
        debug_output("verified NVIDIA-signed Streamline runtime: " +
                     wide_to_utf8(path.c_str()));
    }
    const std::filesystem::path ngx_path = runtime_directory_ / L"nvngx_dlssd.dll";
    if (!std::filesystem::is_regular_file(ngx_path)) {
        error = "required signed NGX DLSS-RR runtime is missing: " +
            wide_to_utf8(ngx_path.c_str());
        return false;
    }
    if (!verify_nvidia_authenticode(ngx_path)) {
        error = "trusted NVIDIA Authenticode verification failed for NGX runtime: " +
            wide_to_utf8(ngx_path.c_str());
        return false;
    }
    debug_output("verified NVIDIA Authenticode publisher for NGX runtime: " +
                 wide_to_utf8(ngx_path.c_str()));
    return true;
}

bool DxrStreamline::initialize(RendererRayReconstructionMode mode,
                               std::string &error)
{
    if (initialized_) {
        error = "Streamline was initialized more than once";
        return false;
    }
    static constexpr std::array<const wchar_t *, 4> runtime_modules = {
        L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss_d.dll",
        L"nvngx_dlssd.dll",
    };
    for (const wchar_t *module : runtime_modules) {
        if (GetModuleHandleW(module) != nullptr) {
            error = "Streamline/NGX runtime was loaded before signature "
                    "verification: " + wide_to_utf8(module);
            return false;
        }
    }
    if (!configure_mode(mode, error) || !verify_runtime(error)) {
        return false;
    }

    const sl::Feature features[] = {sl::kFeatureDLSS_RR};
    const wchar_t *plugin_paths[] = {runtime_directory_.c_str()};
    sl::Preferences preferences{};
    preferences.showConsole = false;
    preferences.logLevel = sl::LogLevel::eDefault;
    preferences.pathsToPlugins = plugin_paths;
    preferences.numPathsToPlugins = static_cast<uint32_t>(std::size(plugin_paths));
    preferences.pathToLogsAndData = nullptr;
    preferences.logMessageCallback = streamline_log;
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
        sl::PreferenceFlags::eUseManualHooking |
        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = static_cast<uint32_t>(std::size(features));
    preferences.applicationId = 0u;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = AB3D2_STREAMLINE_ENGINE_VERSION;
    preferences.projectId = AB3D2_STREAMLINE_PROJECT_ID;
    preferences.renderAPI = sl::RenderAPI::eD3D12;

    const sl::Result init_result = slInit(preferences);
    if (init_result != sl::Result::eOk) {
        error = result_error("slInit(DLSS-RR with custom project GUID)",
                             init_result);
        return false;
    }
    initialized_ = true;

    sl::FeatureRequirements requirements{};
    const sl::Result requirements_result =
        slGetFeatureRequirements(sl::kFeatureDLSS_RR, requirements);
    if (requirements_result != sl::Result::eOk) {
        error = result_error("slGetFeatureRequirements(DLSS-RR)",
                             requirements_result);
        std::string shutdown_error;
        (void)shutdown(shutdown_error);
        return false;
    }
    debug_output(std::string("Streamline DLSS-RR initialized with CUSTOM engine ") +
                 AB3D2_STREAMLINE_ENGINE_VERSION + ", project " +
                 AB3D2_STREAMLINE_PROJECT_ID + ", mode " + mode_name(mode_) +
                 "; OTA disabled; required tags=" +
                 std::to_string(requirements.numRequiredTags));
    return true;
}

bool DxrStreamline::adapter_supported(const LUID &luid, std::string &reason) const
{
    if (!initialized_) {
        reason = "Streamline is not initialized";
        return false;
    }
    sl::AdapterInfo adapter{};
    adapter.deviceLUID = reinterpret_cast<uint8_t *>(
        const_cast<LUID *>(&luid));
    adapter.deviceLUIDSizeInBytes = sizeof(luid);
    const sl::Result result = slIsFeatureSupported(sl::kFeatureDLSS_RR, adapter);
    if (result != sl::Result::eOk) {
        reason = result_error("slIsFeatureSupported(DLSS-RR)", result);
        return false;
    }
    reason.clear();
    return true;
}

bool DxrStreamline::get_native_factory(IDXGIFactory6 *proxy,
                                       IDXGIFactory6 **native,
                                       std::string &error) const
{
    if (!proxy || !native) {
        error = "Streamline factory unwrapping received invalid state";
        return false;
    }
    *native = nullptr;
    const sl::Result result = slGetNativeInterface(proxy,
        reinterpret_cast<void **>(native));
    if (result != sl::Result::eOk || !*native) {
        error = result_error("slGetNativeInterface(IDXGIFactory6)", result);
        return false;
    }
    return true;
}

bool DxrStreamline::get_native_device(ID3D12Device5 *proxy,
                                      ID3D12Device5 **native,
                                      std::string &error) const
{
    if (!proxy || !native) {
        error = "Streamline device unwrapping received invalid state";
        return false;
    }
    *native = nullptr;
    const sl::Result result = slGetNativeInterface(proxy,
        reinterpret_cast<void **>(native));
    if (result != sl::Result::eOk || !*native) {
        error = result_error("slGetNativeInterface(ID3D12Device5)", result);
        return false;
    }
    return true;
}

bool DxrStreamline::set_device(ID3D12Device5 *device, const LUID &luid,
                               std::string &error)
{
    if (!initialized_ || !device) {
        error = "Streamline device setup received incomplete state";
        return false;
    }
    const sl::Result set_result = slSetD3DDevice(device);
    if (set_result != sl::Result::eOk) {
        error = result_error("slSetD3DDevice", set_result);
        return false;
    }
    device_set_ = true;
    std::string support_reason;
    if (!adapter_supported(luid, support_reason)) {
        error = "selected adapter lost DLSS-RR support after device creation: " +
            support_reason;
        return false;
    }

    sl::FeatureVersion version{};
    const sl::Result version_result =
        slGetFeatureVersion(sl::kFeatureDLSS_RR, version);
    if (version_result != sl::Result::eOk) {
        error = result_error("slGetFeatureVersion(DLSS-RR)", version_result);
        return false;
    }
    std::ostringstream report;
    report << "Streamline DLSS-RR feature version " << version.versionSL.major
           << '.' << version.versionSL.minor << '.' << version.versionSL.build
           << ", NGX " << version.versionNGX.major << '.'
           << version.versionNGX.minor << '.' << version.versionNGX.build;
    debug_output(report.str());
    return true;
}

bool DxrStreamline::configure_output(UINT output_width, UINT output_height,
                                     UINT &render_width, UINT &render_height,
                                     std::string &error)
{
    if (!device_set_ || output_width == 0 || output_height == 0) {
        error = "Streamline output configuration received invalid dimensions or device";
        return false;
    }
    if (output_width_ == output_width && output_height_ == output_height &&
        render_width_ != 0 && render_height_ != 0) {
        render_width = render_width_;
        render_height = render_height_;
        return true;
    }
    if (!release_resources(error)) {
        return false;
    }

    UINT selected_width = output_width;
    UINT selected_height = output_height;
    if (active()) {
        sl::DLSSDOptimalSettings settings{};
        const sl::DLSSDOptions options = make_options(
            mode_, output_width, output_height, nullptr);
        const sl::Result result = slDLSSDGetOptimalSettings(options, settings);
        if (result != sl::Result::eOk) {
            error = result_error("slDLSSDGetOptimalSettings", result);
            return false;
        }
        selected_width = settings.optimalRenderWidth;
        selected_height = settings.optimalRenderHeight;
        if (selected_width == 0 || selected_height == 0 ||
            selected_width > output_width || selected_height > output_height ||
            (selected_width == output_width && selected_height == output_height)) {
            error = "DLSS-RR did not provide a valid low-resolution render size for " +
                std::to_string(output_width) + "x" +
                std::to_string(output_height);
            return false;
        }
    }
    output_width_ = output_width;
    output_height_ = output_height;
    render_width_ = selected_width;
    render_height_ = selected_height;
    render_width = render_width_;
    render_height = render_height_;
    debug_output(std::string("DLSS-RR ") + mode_name(mode_) + " dimensions: " +
                 std::to_string(render_width_) + "x" +
                 std::to_string(render_height_) + " -> " +
                 std::to_string(output_width_) + "x" +
                 std::to_string(output_height_));
    return true;
}

bool DxrStreamline::evaluate(
    ID3D12GraphicsCommandList4 *command_list, uint32_t frame_number,
    const reconstruction::CameraProjection &current_camera,
    const reconstruction::CameraProjection &previous_camera,
    reconstruction::PixelJitter jitter, bool history_valid,
    const DxrStreamlineResources &resources, std::string &error)
{
    if (!active()) {
        return true;
    }
    if (!device_set_ || !command_list || !complete_resources(resources) ||
        current_camera.width != render_width_ ||
        current_camera.height != render_height_) {
        error = "DLSS-RR evaluation received an incomplete frame contract";
        return false;
    }

    sl::FrameToken *frame_token = nullptr;
    sl::Result result = slGetNewFrameToken(frame_token, &frame_number);
    if (result != sl::Result::eOk || !frame_token) {
        error = result_error("slGetNewFrameToken", result);
        return false;
    }
    const sl::Constants constants = make_constants(
        current_camera, previous_camera, jitter, history_valid);
    result = slSetConstants(constants, *frame_token, rr_viewport);
    if (result != sl::Result::eOk) {
        error = result_error("slSetConstants(DLSS-RR)", result);
        return false;
    }
    const sl::DLSSDOptions options = make_options(
        mode_, output_width_, output_height_, &current_camera);
    result = slDLSSDSetOptions(rr_viewport, options);
    if (result != sl::Result::eOk) {
        error = result_error("slDLSSDSetOptions", result);
        return false;
    }

    constexpr uint32_t uav_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    std::array<sl::Resource, 10> native_resources = {
        sl::Resource(sl::ResourceType::eTex2d, resources.noisy_radiance,
                     uav_state),
        sl::Resource(sl::ResourceType::eTex2d, resources.output, uav_state),
        sl::Resource(sl::ResourceType::eTex2d, resources.diffuse_albedo,
                     uav_state),
        sl::Resource(sl::ResourceType::eTex2d, resources.specular_albedo,
                     uav_state),
        sl::Resource(sl::ResourceType::eTex2d, resources.shading_normal,
                     uav_state),
        sl::Resource(sl::ResourceType::eTex2d, resources.linear_roughness,
                     uav_state),
        sl::Resource(sl::ResourceType::eTex2d, resources.linear_depth,
                     uav_state),
        sl::Resource(sl::ResourceType::eTex2d, resources.scene_motion,
                     uav_state),
        sl::Resource(sl::ResourceType::eTex2d,
                     resources.specular_hit_distance, uav_state),
        sl::Resource(sl::ResourceType::eTex2d,
                     resources.diffuse_hit_distance, uav_state),
    };
    const sl::Extent input_extent{0, 0, render_width_, render_height_};
    const sl::Extent output_extent{0, 0, output_width_, output_height_};
    std::array<sl::ResourceTag, 10> tags = {
        sl::ResourceTag(&native_resources[0], sl::kBufferTypeScalingInputColor,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[1], sl::kBufferTypeScalingOutputColor,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &output_extent),
        sl::ResourceTag(&native_resources[2], sl::kBufferTypeAlbedo,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[3], sl::kBufferTypeSpecularAlbedo,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[4], sl::kBufferTypeNormals,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[5], sl::kBufferTypeRoughness,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[6], sl::kBufferTypeLinearDepth,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[7], sl::kBufferTypeMotionVectors,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[8], sl::kBufferTypeSpecularHitDistance,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
        sl::ResourceTag(&native_resources[9], sl::kBufferTypeDiffuseHitDistance,
                        sl::ResourceLifecycle::eValidUntilEvaluate,
                        &input_extent),
    };
    result = slSetTagForFrame(
        *frame_token, rr_viewport, tags.data(), static_cast<uint32_t>(tags.size()),
        reinterpret_cast<sl::CommandBuffer *>(command_list));
    if (result != sl::Result::eOk) {
        error = result_error("slSetTagForFrame(DLSS-RR)", result);
        return false;
    }
    const sl::BaseStructure *evaluate_inputs[] = {&rr_viewport};
    result = slEvaluateFeature(
        sl::kFeatureDLSS_RR, *frame_token, evaluate_inputs,
        static_cast<uint32_t>(std::size(evaluate_inputs)),
        reinterpret_cast<sl::CommandBuffer *>(command_list));
    if (result != sl::Result::eOk) {
        error = result_error("slEvaluateFeature(DLSS-RR)", result);
        return false;
    }
    resources_allocated_ = true;
    return true;
}

bool DxrStreamline::release_resources(std::string &error)
{
    if (!resources_allocated_) {
        return true;
    }
    const sl::Result result = slFreeResources(sl::kFeatureDLSS_RR, rr_viewport);
    if (result != sl::Result::eOk) {
        error = result_error("slFreeResources(DLSS-RR)", result);
        return false;
    }
    resources_allocated_ = false;
    return true;
}

bool DxrStreamline::shutdown(std::string &error)
{
    if (!initialized_) {
        return true;
    }
    bool succeeded = true;
    std::string release_error;
    if (resources_allocated_ && !release_resources(release_error)) {
        succeeded = false;
        error = release_error;
    }
    const sl::Result result = slShutdown();
    if (result != sl::Result::eOk) {
        const std::string shutdown_error = result_error("slShutdown", result);
        if (!error.empty()) {
            error += "; ";
        }
        error += shutdown_error;
        succeeded = false;
    }
    initialized_ = false;
    device_set_ = false;
    resources_allocated_ = false;
    output_width_ = 0;
    output_height_ = 0;
    render_width_ = 0;
    render_height_ = 0;
    return succeeded;
}

bool DxrStreamline::active() const
{
    return mode_ != Mode::off;
}

}  // namespace ab3d2::dxr
