idf_component_register(SRCS "DSPEngine.cpp"
                    INCLUDE_DIRS "include" "../../main"
                    REQUIRES Core esp-dsp "bno055"
                    )