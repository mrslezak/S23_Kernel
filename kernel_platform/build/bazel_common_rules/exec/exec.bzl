# Copyright (C) 2022 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@bazel_skylib//lib:shell.bzl", "shell")
load(":exec_aspect.bzl", "ExecAspectInfo", "exec_aspect")

def _impl(ctx):
    out_file = ctx.actions.declare_file(ctx.label.name)

    for target in ctx.attr.data:
        if ExecAspectInfo not in target:
            continue
        if target[ExecAspectInfo].args:
            fail("{}: {} must not have args. Use embedded_exec to wrap it.".format(ctx.label, target.label))
        if target[ExecAspectInfo].env:
            fail("{}: {} must not have env. Use embedded_exec to wrap it.".format(ctx.label, target.label))

    content = "#!{}\n".format(ctx.attr.hashbang)
    content += ctx.attr.script

    content = ctx.expand_location(content, ctx.attr.data)
    ctx.actions.write(out_file, content, is_executable = True)

    runfiles = ctx.runfiles(files = ctx.files.data + [out_file])
    runfiles = runfiles.merge_all([target[DefaultInfo].default_runfiles for target in ctx.attr.data])

    return DefaultInfo(
        files = depset([out_file]),
        executable = out_file,
        runfiles = runfiles,
    )

exec = rule(
    implementation = _impl,
    doc = """Run a script when `bazel run` this target.

See [documentation] for the `args` attribute.
""",
    attrs = {
        "data": attr.label_list(aspects = [exec_aspect], allow_files = True, doc = """A list of labels providing runfiles. Labels may be used in `script`.

Executables in `data` must not have the `args` and `env` attribute. Use
[`embedded_exec`](#embedded_exec) to wrap the depended target so its env and args
are preserved.
"""),
        "hashbang": attr.string(default = "/bin/bash -e", doc = "Hashbang of the script."),
        "script": attr.string(doc = """The script.

Use `$(rootpath <label>)` to refer to the path of a target specified in `data`. See
[documentation](https://bazel.build/reference/be/make-variables#predefined_label_variables).

Use `$@` to refer to the args attribute of this target.

See `build/bazel_common_rules/exec/tests/BUILD` for examples.
"""),
    },
    executable = True,
)

exec_test = rule(
    implementation = _impl,
    doc = """Run a test script when `bazel test` this target.

See [documentation] for the `args` attribute.
""",
    attrs = {
        "data": attr.label_list(aspects = [exec_aspect], allow_files = True, doc = """A list of labels providing runfiles. Labels may be used in `script`.

Executables in `data` must not have the `args` and `env` attribute. Use
[`embedded_exec`](#embedded_exec) to wrap the depended target so its env and args
are preserved.
"""),
        "hashbang": attr.string(default = "/bin/bash -e", doc = "Hashbang of the script."),
        "script": attr.string(doc = """The script.

Use `$(rootpath <label>)` to refer to the path of a target specified in `data`. See
[documentation](https://bazel.build/reference/be/make-variables#predefined_label_variables).

Use `$@` to refer to the args attribute of this target.

See `build/bazel_common_rules/exec/tests/BUILD` for examples.
"""),
    },
    test = True,
)
