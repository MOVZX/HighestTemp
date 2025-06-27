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

/**
 * Signal handler for SIGTERM and SIGINT. Sets the running flag to zero,
 * causing the main loop to exit.
 *
 * @param signum the signal number that was received
 */
static void handle_sigterm(int signum)
{
    (void)signum;

    running = 0;
}

/**
 * Discovers all available temperature sensors and stores their file
 * descriptors in a dynamically allocated array.
 *
 * This function is called once at the start of the program and is
 * responsible for setting up the array of discovered sensors.
 *
 * All file descriptors in the array are opened with O_RDONLY and
 * O_CLOEXEC, so they are all safe to read from and will be automatically
 * closed if the program execs.
 *
 * @note This function does not return a value; instead, it modifies the
 * global variables discovered_sensor_fds, num_discovered_sensors, and
 * sensor_capacity.
 */
static void discover_sensors(void)
{
    glob_t hwmon_paths;

    if (glob("/sys/class/hwmon/hwmon*", 0, NULL, &hwmon_paths) != 0)
        return;

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

        if (strcmp(buffer, "k10temp") == 0 || strcmp(buffer, "amdgpu") == 0 || strcmp(buffer, "coretemp") == 0 || strcmp(buffer, "nvme") == 0)
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
                            close(temp_fd);
                            globfree(&temp_paths);

                            goto cleanup_hwmon;
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

cleanup_hwmon:
    globfree(&hwmon_paths);
}

/**
 * Reads a temperature from a given sensor file descriptor.
 *
 * This function reads up to @c BUFFER_SIZE - 1 bytes from the given
 * sensor file descriptor, attempts to parse the result as a decimal
 * integer, and returns that value. If any step of the process fails,
 * @c INT_MIN is returned.
 *
 * @param sensor_fd the file descriptor of the sensor to read from
 * @return the temperature read from the sensor, or @c INT_MIN on error
 */
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
    long temp = strtol(buffer, &endptr, 10);

    if (endptr == buffer || (*endptr != '\0' && *endptr != '\n'))
        return INT_MIN;

    if (temp > INT_MAX || temp < INT_MIN)
        return INT_MIN;

    return (int)temp;
}

#ifdef NVIDIA
/**
 * Initializes the NVIDIA Management Library (NVML) and selects the first
 * available NVIDIA device for temperature reading.
 *
 * This function is a no-op if the NVML library is unavailable or if the
 * initialization fails.
 */
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

/**
 * Reads the temperature of the NVIDIA GPU.
 *
 * Utilizes the NVIDIA Management Library (NVML) to retrieve the current
 * temperature of the GPU. The temperature is returned in millidegrees
 * Celsius. If the temperature retrieval fails, the function returns
 * INT_MIN to indicate an error.
 *
 * @return the GPU temperature in millidegrees Celsius, or INT_MIN on error
 */
static int read_nvidia_gpu_temperature(void)
{
    unsigned int temp;
    nvmlReturn_t result = nvmlDeviceGetTemperature(nvidia_device, NVML_TEMPERATURE_GPU, &temp);

    if (result != NVML_SUCCESS)
        return INT_MIN;

    return (int)temp * 1000;
}
#endif

/**
 * Frees all dynamically allocated memory, closes all open file descriptors,
 * and resets all global state to its initial values.
 *
 * This function is called when the program is exiting due to a signal or
 * error.
 */
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

/**
 * Main function of the highesttemp daemon.
 *
 * This function initializes logging, sets up signal handling for graceful
 * termination, discovers available temperature sensors, and optionally
 * initializes NVIDIA GPU temperature reading support. It then enters the
 * main loop, which runs until terminated, reading temperatures from
 * discovered sensors and writing the highest temperature to the output
 * file at regular intervals. The output file is created with specified
 * permissions and ownership, and the process drops root privileges after
 * setup is complete.
 *
 * @return 0 on successful completion, 1 on error during initialization.
 */
int main(void)
{
    struct sigaction action;
    struct passwd *pw;
    struct group *gr;
    uid_t target_uid;
    gid_t target_gid;

    openlog("highesttemp", LOG_PID | LOG_CONS, LOG_DAEMON);

    int out_fd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);

    if (out_fd == -1)
    {
        syslog(LOG_ERR, "Failed to open output file %s: %m", outfile);

        cleanup_globals();
        closelog();

        return 1;
    }

    pw = getpwnam("goghor");

    if (pw == NULL)
    {
        syslog(LOG_ERR, "User 'goghor' not found: %m");

        close(out_fd);
        cleanup_globals();
        closelog();

        return 1;
    }

    gr = getgrnam("goghor");

    if (gr == NULL)
    {
        syslog(LOG_ERR, "Group 'goghor' not found: %m");

        close(out_fd);
        cleanup_globals();
        closelog();

        return 1;
    }

    target_uid = pw->pw_uid;
    target_gid = gr->gr_gid;

    if (fchown(out_fd, target_uid, target_gid) == -1)
        syslog(LOG_WARNING, "Failed to chown output file %s: %m", outfile);

    output_fd = out_fd;

    memset(&action, 0, sizeof(struct sigaction));

    action.sa_handler = handle_sigterm;

    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    discover_sensors();

#ifdef NVIDIA
    init_nvidia();
#endif

    if (setgid(target_gid) == -1)
    {
        syslog(LOG_ERR, "Failed to setgid to 'goghor': %m");

        cleanup_globals();
        closelog();

        return 1;
    }

    if (setuid(target_uid) == -1)
    {
        syslog(LOG_ERR, "Failed to setuid to 'goghor': %m");

        cleanup_globals();
        closelog();

        return 1;
    }

    if (setgroups(1, &target_gid) == -1)
        syslog(LOG_WARNING, "Failed to setgroups: %m");

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

        if (highest_current_temp != INT_MIN)
        {
            if (lseek(output_fd, 0, SEEK_SET) == -1)
            {
                syslog(LOG_WARNING, "lseek failed for output file: %m");
            }
            else
            {
                dprintf(output_fd, "%d\n", highest_current_temp);

                if (fsync(output_fd) == -1)
                    syslog(LOG_WARNING, "fsync failed for output file: %m");
            }
        }
    }

    cleanup_globals();
    closelog();

    return 0;
}
