find_program(CAT_EXE NAMES cat)
find_program(GREP_EXE NAMES grep)
find_program(SED_EXE NAMES sed)
find_program(ECHO_EXE NAMES echo)
find_program(SH_EXE NAMES sh)

if(CAT_EXE AND GREP_EXE AND SED_EXE AND ECHO_EXE AND SH_EXE)
    set(HAVE_SUPERNOVAE_TOOLS 1)
else(CAT_EXE AND GREP_EXE AND SED_EXE AND ECHO_EXE AND SH_EXE)
    if(enable_supernovae)
    message("CAT_EXE ${CAT_EXE}")
    message("GREP_EXE ${GREP_EXE}")
    message("SED_EXE ${SED_EXE}")
    message("ECHO_EXE ${ECHO_EXE}")
    message("SH_EXE ${SH_EXE}")
    endif(enable_supernovae)
    set(HAVE_SUPERNOVAE_TOOLS 0)
endif(CAT_EXE AND GREP_EXE AND SED_EXE AND ECHO_EXE AND SH_EXE)

mark_as_advanced(CAT_EXE)
mark_as_advanced(GREP_EXE)
mark_as_advanced(SED_EXE)
mark_as_advanced(ECHO_EXE)
mark_as_advanced(SH_EXE)