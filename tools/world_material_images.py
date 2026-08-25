"""Shared image treatment for native DXR and converted Q2 world materials."""

from __future__ import annotations

from collections.abc import Mapping

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


def clamp_world_normal_blue(image: Image.Image) -> Image.Image:
    """Apply the shared encoded-Z floor used by both runtime packages."""

    red, green, blue, alpha = image.convert("RGBA").split()
    blue = blue.point(lambda value: max(WORLD_NORMAL_BLUE_MINIMUM, value))
    return Image.merge("RGBA", (red, green, blue, alpha))


def resize_world_channels(
    channels: Mapping[str, Image.Image], source_size: tuple[int, int]
) -> dict[str, Image.Image]:
    target_size = scaled_world_size(source_size)
    resized = {
        name: resize_world_channel(image.convert("RGBA"), target_size)
        for name, image in channels.items()
    }
    if "normal" not in resized:
        raise ValueError("world material channels have no normal map")
    resized["normal"] = clamp_world_normal_blue(resized["normal"])
    return resized
