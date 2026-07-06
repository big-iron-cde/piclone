<!--
The site home page is the project README, included verbatim so the two stay
in sync: edit ../README.md and this page updates automatically on rebuild.
The toctrees below are hidden from the body but build the sidebar navigation.
-->

```{include} ../README.md
:relative-images:
```

```{toctree}
:hidden:

Home <self>
```

```{toctree}
:hidden:
:maxdepth: 2
:caption: Hardware

hardware/memory-map
hardware/pinout
hardware/wiring
hardware/power
hardware/bring-up
hardware/troubleshooting
```

```{toctree}
:hidden:
:maxdepth: 2
:caption: Software

firmware
hardware-api
host-tools
```
