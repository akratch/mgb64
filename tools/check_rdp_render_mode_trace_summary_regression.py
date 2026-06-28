#!/usr/bin/env python3
"""ROM-free regression checks for RDP render-mode trace summaries."""

from __future__ import annotations

from pathlib import Path
import tempfile

import summarize_rdp_render_mode_trace as rdp_modes


SURFACE_PROMOTED = (
    "[RDP-MODE] frame=1 tri=80 cmd=0x1 class=room effect=- roommtx=1 "
    "dl_room=18 dl=secondary offset=0x60 raw=0xC81049D8 eff=0xC81049D8 "
    "omh=0x00992C20 geom=0x00012205 cc=0x00F78E4F0EBE2D12 "
    "effcc=0x00F78E4F0EBE2D12 opts=0x00403012 settex=1 texnum=1264 "
    "wh=32x32 tex_used=(1,1) blend=alpha api_blend=alpha_rdp_cvg_memory "
    "rdp_mem=coverage room_cvg=1 water_suppress=0 room_sort=0 alpha=1 "
    "fog=1 texedge=0 noise=0 depth=(test=1,upd=0,cmp=1,prim=0,z=xlu) "
    "decode={z=xlu cvg=wrap zcmp=1 zupd=0 aa=1 imrd=1 clr_on_cvg=1 "
    "cvg_x_alpha=0 alpha_cvg=0 force_bl=1 b1=(0,3,0,2) b2=(0,0,1,0)} "
    "ndc_ok=1 bbox=[0.1,0.2,0.3,0.4] area2=0.01\n"
)


DAM_UNPROMOTED = (
    "[RDP-MODE] frame=122 tri=815 cmd=0x2 class=effect effect=glass_shards "
    "roommtx=0 dl_room=-1 dl=? offset=?0x0 raw=0x0C1849D8 eff=0x0C1849D8 "
    "omh=0x00992C60 geom=0x00060205 cc=0x00F38E4F020A2D12 "
    "effcc=0x00F38E4F020A2D12 opts=0x00000511 settex=0 texnum=-1 "
    "wh=0x0 tex_used=(1,1) blend=alpha api_blend=alpha rdp_mem=none "
    "room_cvg=0 water_suppress=0 room_sort=0 alpha=1 fog=0 texedge=0 "
    "noise=0 depth=(test=1,upd=0,cmp=1,prim=0,z=xlu) "
    "decode={z=xlu cvg=wrap zcmp=1 zupd=0 aa=1 imrd=1 clr_on_cvg=1 "
    "cvg_x_alpha=0 alpha_cvg=0 force_bl=1 b1=(0,3,0,2) b2=(0,0,1,0)} "
    "ndc_ok=1 bbox=[0.2,-0.8,0.3,-0.7] area2=0.003\n"
)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mgb64_rdp_mode_summary_") as tmp:
        path = Path(tmp) / "native.log"
        path.write_text(SURFACE_PROMOTED + DAM_UNPROMOTED, encoding="utf-8")
        rows = rdp_modes.load_rows([path])

        assert len(rows) == 2
        assert rows[0]["api_blend"] == "alpha_rdp_cvg_memory"
        assert rows[0]["rdp_mem"] == "coverage"
        assert rows[0]["room_cvg"] == "1"
        assert not rdp_modes.is_unpromoted_coverage_candidate(rows[0])
        assert rows[1]["class"] == "effect"
        assert rows[1]["effect"] == "glass_shards"
        assert rows[1]["raw"] == "0x0C1849D8"
        assert rows[1]["api_blend"] == "alpha"
        assert rdp_modes.is_unpromoted_coverage_candidate(rows[1])
        assert rdp_modes.main([str(path), "--top", "4"]) == 0

    print("PASS: RDP render-mode trace summary regression")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
