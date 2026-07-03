"""Sphinx configuration for the Piclone documentation website.

Combines:
  * Doxygen (C firmware in src/) via Breathe
  * Prose written in MyST Markdown

Host-side control lives in the separate Romulan repository, so it is documented
as prose (see host-tools.md) rather than via autodoc.
"""

from pathlib import Path

DOCS_DIR = Path(__file__).resolve().parent

# -- Project information -----------------------------------------------------

project = "Piclone"
author = "big-iron-cde"
copyright = "2026, big-iron-cde"
release = "0.1"

# -- General configuration ---------------------------------------------------

extensions = [
    "breathe",
    "myst_parser",
    "sphinxcontrib.mermaid",
]

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "fieldlist",
    "tasklist",
]
myst_heading_anchors = 3

# Render GitHub-style ```mermaid fenced code blocks (used in README.md) as
# mermaid diagrams instead of plain code listings.
myst_fence_as_directive = ["mermaid"]

templates_path = ["_templates"]
exclude_patterns = ["_build", "_doxygen", "Thumbs.db", ".DS_Store"]

# -- Breathe (Doxygen bridge) ------------------------------------------------

breathe_projects = {"piclone": str(DOCS_DIR / "_doxygen" / "xml")}
breathe_default_project = "piclone"
breathe_domain_by_extension = {"h": "c", "c": "c"}

# -- HTML output -------------------------------------------------------------

html_theme = "furo"
html_title = "Piclone"
html_static_path = ["_static"]
