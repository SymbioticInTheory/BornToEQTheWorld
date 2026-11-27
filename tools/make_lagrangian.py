"""Renders the Standard Model Lagrangian (the compact form on the CERN mug)
as a white-on-transparent PNG. The standalone's title bar draws it tinted
with the accent colour. Needs matplotlib.

    python tools/make_lagrangian.py resources/lagrangian.png
"""
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

FORMULA = (
    r"$\mathcal{L} = -\frac{1}{4}F_{\mu\nu}F^{\mu\nu}"
    r" + i\bar{\psi}\,D\!\!\!/\,\psi + \text{h.c.}"
    r" + \bar{\psi}_i\, y_{ij}\, \psi_j\, \phi + \text{h.c.}"
    r" + |D_\mu \phi|^2 - V(\phi)$"
)


def main(out):
    matplotlib.rcParams["mathtext.fontset"] = "cm"
    fig = plt.figure(figsize=(1, 1), dpi=100)
    fig.patch.set_alpha(0)
    fig.text(0, 0, FORMULA, fontsize=40, color="white")
    fig.savefig(out, dpi=100, transparent=True, bbox_inches="tight", pad_inches=0.04)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "resources/lagrangian.png")
