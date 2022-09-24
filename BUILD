cc_binary(
    name = "cprd",
    srcs = ["main.cc"],
    deps = [
        ":state",
        ":state_cc_proto",
        "@ncurses//:main",
    ],
)

proto_library(
    name = "state_proto",
    srcs = ["state.proto"],
)

cc_proto_library(
    name = "state_cc_proto",
    deps = [":state_proto"],
)

cc_library(
    name = "state",
    srcs = ["state.cc"],
    hdrs = ["state.h"],
    deps = [
        ":state_cc_proto",
    ],
)
