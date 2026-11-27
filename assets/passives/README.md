# Passive counter references

`generate_references.py` creates compact artwork and numeral samples for the shared passive HUD reader. The detector finds artwork independently of its progress ring and reads the overlapping counter separately. Nothing in the reader depends on a recorded frame, screen coordinate, or equipment slot.

Star Saw artwork: [BSS StarSaw](https://www.youtube.com/watch?v=ZeHCQBeXA2g). Use the square icon without the surrounding video border. Numeral samples use Roblox's HWYGOTH.ttf and BuilderSans-Bold.otf, generated at 24 pixels. The application does not load these fonts.

Independent gameplay used for validation: [Spider boost recording](https://www.youtube.com/watch?v=yRe-ScqtDUc). The small counter changes through 0, 1 and 2; unreadable frames remain unknown. A 2-to-0 transition confirms activation after a Stinger press. The counter alone does not indicate whether an already-running Saw is active when observation begins.
