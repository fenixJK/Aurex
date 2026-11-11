"""Print a FixedLabel reference from a font, without changing runtime logic.

Example: python generate_label_reference.py SourceSansPro-Semibold.ttf \
  'Complete!' --name quest_complete_native --size 22 --threshold 90
Requires Pillow. The supplied font is used only while generating the mask.
"""
import argparse

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font")
    parser.add_argument("label")
    parser.add_argument("--name", default="label_reference")
    parser.add_argument("--size", type=int, default=22)
    parser.add_argument("--threshold", type=int, default=90)
    args = parser.parse_args()
    font = ImageFont.truetype(args.font, args.size)
    bounds = font.getbbox(args.label)
    image = Image.new("L", (bounds[2] - bounds[0] + 4, bounds[3] - bounds[1] + 4))
    ImageDraw.Draw(image).text((2 - bounds[0], 2 - bounds[1]), args.label, font=font, fill=255)
    image = image.point(lambda value: 255 if value > args.threshold else 0)
    image = image.crop(image.getbbox())
    rows = []
    for y in range(image.height):
        for start in range(0, image.width, 64):
            value = sum(bool(image.getpixel((x, y))) << (x - start) for x in range(start, min(start + 64, image.width)))
            rows.append(f"0x{value:x}ULL")
    print(f"inline constexpr std::array<std::uint64_t,{len(rows)}> {args.name}" + "{" + ",".join(rows) + "};")
    print(f'FixedLabel{{"{args.label}",{image.width},{image.height},{args.name}}},')


if __name__ == "__main__":
    main()
