#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define DT 1000000
#define BUFFER_SIZE 16
#define MAX_TEMP -2147483648

const char* sensors[] =
{
    // HW Sensors on my PC
    "/sys/class/hwmon/hwmon2/temp1_input", // NVMe 1
    "/sys/class/hwmon/hwmon3/temp1_input", // NVMe 2
    "/sys/class/hwmon/hwmon4/temp1_input", // NVMe 3
    "/sys/class/hwmon/hwmon5/temp1_input", // NVMe 4
    "/sys/class/hwmon/hwmon6/temp1_input", // GPU
    "/sys/class/hwmon/hwmon7/temp1_input"  // CPU
};

// Save the highest value of sensor readings to TMPFS
const char* outfile = "/tmp/highesttemp";

int read_temperature(const char* sensor_path)
{
    int fd = open(sensor_path, O_RDONLY);

    if (fd == -1)
    {
        perror("Error opening sensor file");
        return -1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);

    close(fd);

    if (bytes_read <= 0)
    {
        perror("Error reading sensor file");

        return -1;
    }

    buffer[bytes_read] = '\0';

    return atoi(buffer);
}

int main() {
    FILE* myfile = fopen(outfile, "w");

    if (!myfile)
    {
        perror("Error opening output file");

        return 1;
    }

    int max_temp = MAX_TEMP;

    while (1) {
        usleep(DT);

        int highest_current_temp = MAX_TEMP;

        for (int i = 0; i < sizeof(sensors) / sizeof(sensors[0]); i++)
        {
            int temp = read_temperature(sensors[i]);

            if (temp != -1)
            {
                highest_current_temp = (temp > highest_current_temp) ? temp : highest_current_temp;
            }
            else
            {
                fprintf(stderr, "Error reading sensor %s\n", sensors[i]);
            }
        }

        // Debug
        // printf("Highest Temp: %d\n", highest_current_temp);

        rewind(myfile);
        fprintf(myfile, "%d\n\n", highest_current_temp);
        fflush(myfile);
        ftruncate(fileno(myfile), ftell(myfile));
    }

    fclose(myfile);

    return 0;
}
