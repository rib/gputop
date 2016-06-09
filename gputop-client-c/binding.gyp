{
  "targets": [
    {
      "target_name": "gputop-client-c",
      "defines": [
        "GPUTOP_CLIENT"
      ],
      "include_dirs": [
        ".",
        # Urgh, what a pain...
        '<!(node -e "require(\'child_process\').execSync(\'git rev-parse --show-toplevel\', {stdio:[0,1,2]});")/gputop-server',
        '<!(node -e "require(\'child_process\').execSync(\'git rev-parse --show-toplevel\', {stdio:[0,1,2]});")/gputop-data'
      ],
      "sources": [
        "oa-hsw.h",
        "oa-hsw.c",
        "oa-bdw.h",
        "oa-bdw.c",
        "oa-chv.h",
        "oa-chv.c",
        "oa-skl.h",
        "oa-skl.c",
        "gputop-oa-counters.h",
        "gputop-oa-counters.c",
        "gputop-client-c-runtime.h",
        "gputop-client-c-runtime.c",
        "gputop-client-c-runtime-bindings.cpp",
        "gputop-client-c.c",
        "gputop-client-c-bindings.h",
        "gputop-client-c-bindings.cpp",
      ],
    }
  ]
}
