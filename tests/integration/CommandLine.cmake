# A broken QPA backend proves these commands do not initialize a GUI.
set(cli_config "${CMAKE_CURRENT_BINARY_DIR}/cli-config")
file(MAKE_DIRECTORY "${cli_config}")
file(REMOVE "${cli_config}/dzurkel/dzurkel.ini")
function(check_cli expected_code expected_text)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "XDG_CONFIG_HOME=${cli_config}"
        QT_QPA_PLATFORM=dzurkel_nonexistent_platform "${DZURKEL}" ${ARGN}
        RESULT_VARIABLE code OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT "${code}" STREQUAL "${expected_code}")
        message(FATAL_ERROR "${ARGN}: expected ${expected_code}, got ${code}: ${errors}")
    endif()
    if(NOT "${output}${errors}" MATCHES "${expected_text}")
        message(FATAL_ERROR "${ARGN}: expected '${expected_text}', got: ${output}${errors}")
    endif()
endfunction()

check_cli(0 "Usage:" --help)
check_cli(0 "dzurkel ${EXPECTED_VERSION}" --version)
check_cli(2 "exactly one file path or URL")
check_cli(2 "exactly one file path or URL" first second)
check_cli(2 "Choose exactly one command" --hide --quit)
check_cli(1 "Unsupported URL scheme" --show https://example.com/file)
check_cli(1 "experimental and disabled" --show sftp://server/file.txt)
# Filesystem errors are asynchronous; syntax errors still fail without a GUI.
check_cli(1 "without passwords" --show smb://user:password@server/share/file)

check_cli(2 "does not accept a file" --hide file.txt)
check_cli(2 "does not accept a file" --quit file.txt)
check_cli(2 "exactly one file path or URL" --toggle)
check_cli(2 "Choose exactly one command" --show --toggle file.txt)
check_cli(2 "Unknown option" --unknown)
