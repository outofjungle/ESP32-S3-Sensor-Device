# ESP-IDF Docker Image for the ESP32-S3 Sensor Device project.
#
# This project has no Matter dependency, so it uses the plain ESP-IDF image
# instead of the much larger espressif/esp-matter image used by sibling
# projects such as Matter-M5NanoC6-Switch.

FROM espressif/idf:v6.1

WORKDIR /project

CMD ["/bin/bash"]
