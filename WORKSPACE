load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

git_repository(
    name = "googletest",
    remote = "https://github.com/google/googletest",
    tag = "release-1.8.1",
)

new_local_repository(
    name = "ncurses",
    build_file = "ncurses.BUILD",
    path = "/Users/hosang/homebrew/opt/ncurses",  # install with homebrew
    # export LDFLAGS="-L/Users/hosang/homebrew/opt/ncurses/lib"
    # export CPPFLAGS="-I/Users/hosang/homebrew/opt/ncurses/include"
)
