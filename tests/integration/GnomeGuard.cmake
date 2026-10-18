# A native gate must fail, not silently skip, on an offscreen backend.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen
        DZURKEL_REQUIRE_GNOME_NATIVE=1 "${PREVIEW_TESTS}"
        gnomePresentationKeepsPendingContentAndDismissal
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(NOT result EQUAL 1 OR NOT output MATCHES "Native GNOME was required; refusing to silently skip")
    message(FATAL_ERROR "Native backend guard did not fail explicitly: result=${result}\n${output}\n${error}")
endif()
if(output MATCHES "SKIP")
    message(FATAL_ERROR "Native backend guard silently skipped a required regression")
endif()
