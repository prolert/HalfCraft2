"""the world shaders halfcraft compiles (halfcraft-hl2dm.patch). make build compiles them into build\\shaders\\out;
the game folders get them in shaders\\fxc, where the engine finds them before half-life 2's vpk.

the stock world flashlight shaders with halfcraft's edits keep their stock names, so stdshader_dx9.dll draws with
them; both engines take the same files (the stock ones are byte for byte the same in half-life 2 and hl2:dm). the
hc_ ones are game_shader_generic_halfcraft.dll's (hl2dm only), which leave the cells minecraft dug undrawn.
"""

from dataclasses import dataclass
from pathlib import Path

from halfcraft import REPO

OUT = REPO / "build/shaders/out"


@dataclass(frozen=True)
class Shader:
    source: str  # in the sdk's materialsystem\stdshaders
    name: str  # the .vcs it compiles to
    # the stock combo layout, which stdshader_dx9.dll indexes the compiled shader by (None: halfcraft's own shader,
    # which its dll indexes by the .inc of the same compile)
    combos: int | None = None
    dynamic: int | None = None
    dll: bool = False  # game_shader_generic_halfcraft.dll's: only an engine with Engine.shader_dll draws with it

    @property
    def vcs(self) -> Path:
        return OUT / f"{self.name}.vcs"


SHADERS = (
    Shader("flashlight_ps2x.fxc", "flashlight_ps20b", 1152, 4),
    Shader("lightmappedgeneric_flashlight_vs20.fxc", "lightmappedgeneric_flashlight_vs20", 32, 2),
    Shader("hc_lightmappedgeneric_ps2x.fxc", "hc_lightmappedgeneric_ps20b", dll=True),
    Shader("hc_flashlight_ps2x.fxc", "hc_flashlight_ps20b", dll=True),
)
