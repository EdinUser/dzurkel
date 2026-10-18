# Shared clean-checkout archive policy for Debian and Arch builds.
prepare_source() {
    version=$(sed -n 's/^project(Dzurkel VERSION \([0-9][0-9.]*\) LANGUAGES CXX)/\1/p' CMakeLists.txt)
    [[ -n "$version" ]]
    if [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
        echo 'Commit source changes before packaging; the source archive uses HEAD.' >&2
        exit 1
    fi
    if [[ ${GITHUB_REF:-} == refs/tags/* && ${GITHUB_REF#refs/tags/} != "v$version" ]]; then
        echo 'Release tag must match the package version.' >&2
        exit 1
    fi
    package_dir="$PWD/$1"
    rm -rf "$package_dir"
    mkdir -p "$package_dir"
    git archive --format=tar.gz --prefix="dzurkel-$version/" HEAD > "$package_dir/dzurkel-$version.tar.gz"
    tar -xzf "$package_dir/dzurkel-$version.tar.gz" -C "$package_dir"
}
