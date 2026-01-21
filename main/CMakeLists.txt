set(CURRENT_SRCS "main.cpp")
set(CURRENT_REQUIRES "bno055"
                     "led"
                     "Core"
                     "network"
                     "calculate")

idf_component_register(SRCS ${CURRENT_SRCS}
                       PRIV_REQUIRES ${CURRENT_REQUIRES}
                       REQUIRES esp-dsp
                       INCLUDE_DIRS ".")
