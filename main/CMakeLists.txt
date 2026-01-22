set(CURRENT_SRCS "main.cpp")
set(CURRENT_REQUIRES "bno055"
                     "led"
                     "Core"
                     "network"
                     "calculate"
                    #  "OTAServer"
                    )

idf_component_register(SRCS ${CURRENT_SRCS}
                       PRIV_REQUIRES ${CURRENT_REQUIRES}
                       INCLUDE_DIRS ".")
