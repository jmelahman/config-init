from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface

import manygo

# Must match the solod.dev version in so/go.mod.
SOLOD_VERSION = "v0.3.0"

# solod's os package needs POSIX, so Windows is not a supported target.
ZIG_TARGETS = {
    ("linux", "amd64"): "x86_64-linux-musl",
    ("linux", "arm64"): "aarch64-linux-musl",
    ("darwin", "amd64"): "x86_64-macos-none",
    ("darwin", "arm64"): "aarch64-macos-none",
}


class SolodBinaryBuildHook(BuildHookInterface):
    def initialize(self, version, build_data) -> None:  # noqa: ANN001, ARG002
        if self.target_name != "wheel":
            # The sdist carries sources only; the binary is built when the
            # sdist is turned into a wheel on the installing machine.
            return
        build_data["pure_python"] = False
        goos = os.getenv("GOOS")
        goarch = os.getenv("GOARCH")
        if manygo.is_goos(goos) and manygo.is_goarch(goarch):
            build_data["tag"] = "py3-none-" + manygo.get_platform_tag(goos=goos, goarch=goarch)
        else:
            # Native build: let hatchling tag the wheel for this platform.
            build_data["infer_tag"] = True
        binary_name = self.config["binary_name"]

        # Always rebuild: a leftover binary from another target would be
        # packaged into a wheel tagged for the wrong platform.
        print(f"Building solod binary '{binary_name}'...")
        so = _ensure_so(self.root)
        env = os.environ.copy()
        env["PYTHON"] = sys.executable  # lets scripts/zigcc find ziglang
        zigcc = str(Path(self.root) / "scripts" / "zigcc")
        if goos and goarch:
            try:
                target = ZIG_TARGETS[(goos, goarch)]
            except KeyError:
                raise RuntimeError(f"unsupported target {goos}/{goarch}") from None
            env["CC"] = zigcc
            env["CFLAGS"] = f"--target={target} -Os"
        else:
            # Native build: the system compiler, or zig if there is none.
            if shutil.which(env.get("CC", "cc")) is None:
                env["CC"] = zigcc
            env["CFLAGS"] = "-Os"
        subprocess.check_call(  # noqa: S603
            [so, "build", "-panic=exit", "-o", binary_name, "./so"],
            cwd=self.root,
            env=env,
        )

        build_data["shared_scripts"] = {binary_name: binary_name}


def _ensure_so(root: str) -> str:
    """Return a path to the solod `so` tool, installing it if needed.

    Uses `go` from PATH — inside pip's build isolation that is the go-bin
    package listed in build-system requires.
    """
    so = shutil.which("so")
    if so is not None:
        return so
    gobin = Path(root) / ".so-bin"
    gobin.mkdir(exist_ok=True)
    go = shutil.which("go")
    if go is None:
        raise RuntimeError("go is required to install the solod toolchain")
    env = os.environ.copy()
    env["GOBIN"] = str(gobin)
    # GOOS/GOARCH describe the wheel's target; the so tool itself must be a
    # host binary (and `go install` refuses cross-compiles with GOBIN set).
    env.pop("GOOS", None)
    env.pop("GOARCH", None)
    print(f"Installing solod.dev/cmd/so@{SOLOD_VERSION}...")
    subprocess.check_call(  # noqa: S603
        [go, "install", f"solod.dev/cmd/so@{SOLOD_VERSION}"],
        env=env,
    )
    return str(gobin / "so")
