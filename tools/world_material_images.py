"""Shared image treatment for native DXR and converted Q2 world materials."""

from __future__ import annotations

from collections.abc import Mapping, Sequence

from PIL import Image


WORLD_TEXTURE_SCALE = 4
WORLD_NORMAL_BLUE_MINIMUM = 96


def scaled_world_size(
    source_size: tuple[int, int], scale: int = WORLD_TEXTURE_SCALE
) -> tuple[int, int]:
    width, height = source_size
    if width <= 0 or height <= 0 or scale <= 0:
        raise ValueError("world material dimensions and scale must be positive")
    return width * scale, height * scale


def crop_to_aspect(image: Image.Image, target_size: tuple[int, int]) -> Image.Image:
    """Center-crop to the target aspect using the established package rule."""

    width, height = image.size
    target_width, target_height = target_size
    if width <= 0 or height <= 0 or target_width <= 0 or target_height <= 0:
        raise ValueError("material image dimensions must be positive")
    aspect = target_width / target_height
    current = width / height
    if abs(current - aspect) < 0.001:
        return image
    if current > aspect:
        new_width = max(1, round(height * aspect))
        left = max(0, (width - new_width) // 2)
        return image.crop((left, 0, left + new_width, height))
    new_height = max(1, round(width / aspect))
    top = max(0, (height - new_height) // 2)
    return image.crop((0, top, width, top + new_height))


def resize_world_channel(
    image: Image.Image, target_size: tuple[int, int]
) -> Image.Image:
    return crop_to_aspect(image, target_size).resize(
        target_size, Image.Resampling.LANCZOS
    )


def register_world_channel(
    image: Image.Image,
    horizontal_registration: Sequence[Sequence[float]],
) -> Image.Image:
    """Piecewise-register authored horizontal landmarks to source UV space."""

    points = [tuple(float(value) for value in point) for point in horizontal_registration]
    if (
        len(points) < 2
        or any(len(point) != 2 for point in points)
        or points[0] != (0.0, 0.0)
        or points[-1] != (1.0, 1.0)
        or any(
            not 0.0 <= source <= 1.0 or not 0.0 <= target <= 1.0
            for source, target in points
        )
        or any(
            current[0] <= previous[0] or current[1] <= previous[1]
            for previous, current in zip(points, points[1:])
        )
    ):
        raise ValueError(
            "world material registration points must increase from (0, 0) to (1, 1)"
        )

    width, height = image.size
    output = Image.new(image.mode, image.size)
    for point_index in range(len(points) - 1):
        source_left = round(points[point_index][0] * width)
        source_right = round(points[point_index + 1][0] * width)
        target_left = round(points[point_index][1] * width)
        target_right = round(points[point_index + 1][1] * width)
        if point_index == len(points) - 2:
            source_right = width
            target_right = width
        if source_right <= source_left or target_right <= target_left:
            raise ValueError("world material registration collapses an image segment")
        segment = image.crop((source_left, 0, source_right, height))
        target_size = (target_right - target_left, height)
        if segment.size != target_size:
            segment = segment.resize(target_size, Image.Resampling.LANCZOS)
        output.paste(segment, (target_left, 0))
    return output


def clamp_world_normal_blue(image: Image.Image) -> Image.Image:
    """Apply the shared encoded-Z floor used by both runtime packages."""

    red, green, blue, alpha = image.convert("RGBA").split()
    blue = blue.point(lambda value: max(WORLD_NORMAL_BLUE_MINIMUM, value))
    return Image.merge("RGBA", (red, green, blue, alpha))


def resize_world_channels(
    channels: Mapping[str, Image.Image],
    source_size: tuple[int, int],
    horizontal_registration: Sequence[Sequence[float]] | None = None,
) -> dict[str, Image.Image]:
    target_size = scaled_world_size(source_size)
    resized = {
        name: resize_world_channel(image.convert("RGBA"), target_size)
        for name, image in channels.items()
    }
    if horizontal_registration is not None:
        resized = {
            name: register_world_channel(image, horizontal_registration)
            for name, image in resized.items()
        }
    if "normal" not in resized:
        raise ValueError("world material channels have no normal map")
    resized["normal"] = clamp_world_normal_blue(resized["normal"])
    return resized
