/*
 * highesttemp - A daemon to find and export the highest system temperature.
 *
 * Copyright (C) 2025 MOVZX
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <glob.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>

#ifdef NVIDIA
#include <nvml.h>
#endif

#define DT 1000000
#define BUFFER_SIZE 32
#define SENSOR_INITIAL_CAPACITY 8

static int *discovered_sensor_fds = NULL;
static int num_discovered_sensors = 0;
static int sensor_capacity = 0;
static int output_fd = -1;

#ifdef NVIDIA
static nvmlDevice_t nvidia_device;
#endif

static const char *outfile = "/dev/shm/highesttemp";
static volatile sig_atomic_t running = 1;

static void handle_sigterm(int signum)
{
    (void)signum;

    running = 0;
}

static int discover_sensors(void)
{
    glob_t hwmon_paths;

    if (glob("/sys/class/hwmon/hwmon*", 0, NULL, &hwmon_paths) != 0)
        return 0;

    for (size_t i = 0; i < hwmon_paths.gl_pathc; i++)
    {
        char name_path[PATH_MAX];

        snprintf(name_path, sizeof(name_path), "%s/name", hwmon_paths.gl_pathv[i]);

        int fd = open(name_path, O_RDONLY | O_CLOEXEC);

        if (fd == -1)
            continue;

        char buffer[BUFFER_SIZE];
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);

        close(fd);

        if (bytes_read <= 0)
            continue;

        buffer[bytes_read] = '\0';
        char *nl = strchr(buffer, '\n');

        if (nl)
            *nl = '\0';

        if (strcmp(buffer, "k10temp") == 0 || strcmp(buffer, "amdgpu") == 0 || strcmp(buffer, "coretemp") == 0)
        {
            char temp_pattern[PATH_MAX];

            snprintf(temp_pattern, sizeof(temp_pattern), "%s/temp*_input", hwmon_paths.gl_pathv[i]);

            glob_t temp_paths;

            if (glob(temp_pattern, 0, NULL, &temp_paths) == 0)
            {
                for (size_t j = 0; j < temp_paths.gl_pathc; j++)
                {
                    int temp_fd = open(temp_paths.gl_pathv[j], O_RDONLY | O_CLOEXEC);

                    if (temp_fd == -1)
                        continue;

                    if (num_discovered_sensors >= sensor_capacity)
                    {
                        int new_capacity = (sensor_capacity == 0) ? SENSOR_INITIAL_CAPACITY : sensor_capacity * 2;
                        int *new_sensors = realloc(discovered_sensor_fds, new_capacity * sizeof(int));

                        if (new_sensors == NULL)
                        {
                            syslog(LOG_ERR, "Failed to reallocate memory for sensors: %m");

                            close(temp_fd);
                            globfree(&temp_paths);
                            globfree(&hwmon_paths);

                            return -1;
                        }

                        discovered_sensor_fds = new_sensors;
                        sensor_capacity = new_capacity;
                    }

                    discovered_sensor_fds[num_discovered_sensors++] = temp_fd;
                }

                globfree(&temp_paths);
            }
        }
    }

    globfree(&hwmon_paths);

    return 0;
}

static int read_temperature(int sensor_fd)
{
    char buffer[BUFFER_SIZE];

    if (lseek(sensor_fd, 0, SEEK_SET) == -1)
        return INT_MIN;

    ssize_t bytes_read = read(sensor_fd, buffer, sizeof(buffer) - 1);

    if (bytes_read <= 0)
        return INT_MIN;

    buffer[bytes_read] = '\0';
    char *endptr;
    errno = 0;
    long temp = strtol(buffer, &endptr, 10);

    if (endptr == buffer || (*endptr != '\0' && *endptr != '\n') || errno == ERANGE)
        return INT_MIN;

    if (temp > INT_MAX || temp < INT_MIN)
        return INT_MIN;

    return (int)temp;
}

#ifdef NVIDIA
static void init_nvidia(void)
{
    if (nvmlInit_v2() != NVML_SUCCESS)
        return;

    if (nvmlDeviceGetHandleByIndex_v2(0, &nvidia_device) != NVML_SUCCESS)
    {
        nvmlShutdown();

        return;
    }
}

static int read_nvidia_gpu_temperature(void)
{
    unsigned int temp;
    nvmlReturn_t result = nvmlDeviceGetTemperature(nvidia_device, NVML_TEMPERATURE_GPU, &temp);

    if (result != NVML_SUCCESS)
        return INT_MIN;

    if (temp > (INT_MAX / 1000))
        return INT_MIN;

    return (int)temp * 1000;
}
#endif

static void cleanup_globals(void)
{
    for (int i = 0; i < num_discovered_sensors; i++)
    {
        close(discovered_sensor_fds[i]);
    }

    free(discovered_sensor_fds);

    discovered_sensor_fds = NULL;
    num_discovered_sensors = 0;
    sensor_capacity = 0;

    if (output_fd != -1)
    {
        close(output_fd);
        output_fd = -1;
    }

#ifdef NVIDIA
    nvmlShutdown();
#endif
}

int main(void)
{
    struct sigaction action;
    int last_written_temp = INT_MIN;

    openlog("highesttemp", LOG_PID | LOG_CONS, LOG_DAEMON);

    int out_fd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);

    if (out_fd == -1)
    {
        syslog(LOG_ERR, "Failed to open output file %s: %m", outfile);

        closelog();

        return 1;
    }

    output_fd = out_fd;

    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);

    if (discover_sensors() == -1)
    {
        syslog(LOG_ERR, "Failed to discover sensors.");

        cleanup_globals();
        closelog();

        return 1;
    }

#ifdef NVIDIA
    init_nvidia();
#endif

    while (running)
    {
        usleep(DT);

        int highest_current_temp = INT_MIN;

        for (int i = 0; i < num_discovered_sensors; i++)
        {
            int temp = read_temperature(discovered_sensor_fds[i]);

            if (temp > highest_current_temp)
                highest_current_temp = temp;
        }

#ifdef NVIDIA
        int nvidia_temp = read_nvidia_gpu_temperature();

        if (nvidia_temp > highest_current_temp)
            highest_current_temp = nvidia_temp;
#endif

        if (highest_current_temp != INT_MIN && highest_current_temp != last_written_temp)
        {
            if (lseek(output_fd, 0, SEEK_SET) == -1)
            {
                syslog(LOG_WARNING, "lseek failed for output file: %m");
            }
            else
            {
                int n_written = dprintf(output_fd, "%d\n", highest_current_temp);

                if (n_written > 0)
                {
                    if (ftruncate(output_fd, n_written) == -1)
                        syslog(LOG_WARNING, "ftruncate failed for output file: %m");

                    last_written_temp = highest_current_temp;
                }
                else
                {
                    syslog(LOG_WARNING, "dprintf failed for output file: %m");
                }
            }
        }
    }

    cleanup_globals();
    closelog();

    return 0;
}
