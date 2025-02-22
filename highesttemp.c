#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define DT          1000000          // 1 second sleep
#define BUFFER_SIZE 16              // Enough for temperature values
#define MAX_TEMP    -2147483648     // -2147483648 = 0x80000000
#define NUM_SENSORS 5               // Number of hardware sensors

static const char* sensors[] =
{
    // HW Sensors on my PC
    "/sys/class/hwmon/hwmon2/temp1_input", // NVMe 1
    "/sys/class/hwmon/hwmon3/temp1_input", // NVMe 2
    "/sys/class/hwmon/hwmon4/temp1_input", // NVMe 3
    "/sys/class/hwmon/hwmon5/temp1_input", // NVMe 4
    "/sys/class/hwmon/hwmon6/temp1_input"  // CPU
};

// Save the highest value of sensor readings to TMPFS
static const char* outfile = "/tmp/highesttemp";

int read_temperature(const char* sensor_path)
{
    int fd = open(sensor_path, O_RDONLY);

    if (fd == -1)
    {
        fprintf(stderr, "Error opening sensor file %s: %s\n", sensor_path, strerror(errno));

        return -1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);

    close(fd);

    if (bytes_read <= 0)
    {
        fprintf(stderr, "Error reading sensor file %s: %s\n", sensor_path, strerror(errno));

        close(fd);

        return -1;
    }

    buffer[bytes_read] = '\0';

    return atoi(buffer);
}

int read_nvidia_gpu_temperature()
{
    FILE* fp = popen("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits", "r");

    if (!fp)
    {
        fprintf(stderr, "Error executing nvidia-smi: %s\n", strerror(errno));

        return -1;
    }

    char buffer[BUFFER_SIZE];

    if (!fgets(buffer, sizeof(buffer), fp))
    {
        fprintf(stderr, "Error reading Nvidia GPU temperature: %s\n", strerror(errno));

        pclose(fp);

        return -1;
    }

    pclose(fp);

    return atoi(buffer) * 1000;
}

int main()
{
    FILE* myfile = fopen(outfile, "w");

    if (!myfile)
    {
        fprintf(stderr, "Error opening output file %s: %s\n", outfile, strerror(errno));

        return 1;
    }

    setlinebuf(myfile);

    while (1)
    {
        usleep(DT);

        int highest_current_temp = MAX_TEMP;

        for (int i = 0; i < NUM_SENSORS; i++)
        {
            int temp = read_temperature(sensors[i]);

            if (temp != -1 && temp > highest_current_temp)
            highest_current_temp = temp;
        }

        int nvidia_temp = read_nvidia_gpu_temperature();

        if (nvidia_temp != -1 && nvidia_temp > highest_current_temp)
        highest_current_temp = nvidia_temp;

        if (highest_current_temp != MAX_TEMP)
        {
            rewind(myfile);
            fprintf(myfile, "%d\n", highest_current_temp);
            ftruncate(fileno(myfile), ftell(myfile));
        }

        // Debug
        // printf("Highest Temp: %d\n", highest_current_temp);

    }

    fclose(myfile);

    return 0;
}
