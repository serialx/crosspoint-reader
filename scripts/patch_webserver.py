"""Build the checked multipart parser without changing the installed Arduino framework."""

import hashlib
from pathlib import Path
import subprocess

Import("env")  # noqa: F821


def checked_parser(build_env, node):
    source = Path(node.srcnode().get_abspath())
    if hashlib.sha256(source.read_bytes()).hexdigest() != "522a46a1b8bed19b5482b65eb96bb87fe068c937c4516d17111179b2e8b88adc":
        raise RuntimeError("Review the upload parser patch against this Arduino WebServer revision")
    patch = Path(build_env.subst("$PROJECT_DIR/scripts/webserver-parser.patch"))
    target = Path(build_env.subst("$BUILD_DIR")) / "checked-webserver" / "Parsing.cpp"
    target.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["patch", "-s", "-o", "-", str(source), str(patch)], check=True, stdout=subprocess.PIPE)
    if not target.exists() or target.read_bytes() != result.stdout:
        target.write_bytes(result.stdout)
    parser_env = build_env.Clone()
    parser_env.Prepend(CPPPATH=[str(source.parent), build_env.subst("$PROJECT_DIR/src/network")])
    return parser_env.Object(target=str(target.with_suffix(".cpp.o")), source=str(target))


env.AddBuildMiddleware(checked_parser, "*/WebServer/src/Parsing.cpp")  # noqa: F821
