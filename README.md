# A simple code to get the highest values from various sensor sources

GNU/Linux FanControl app can only be set to use single sensor source, meanwhile I want to make it work with multiple sensor sources; hence I did this.

## Why?
- By default I can only assign a single sensor source to a fan, I usually set my 2 bottom fans to stop/spin based on GPU temperature
- But after some experiences, I want those glorious 2 bottom fans to spin when the CPU or NVMes got high too

## How does it work?
- Basically, it's collecting the sensor values from my CPU, GPU, 1st NVMe, 2nd NVMe, 3rd NVMe, and 4th NVMe
- Then get the highest values from all of those 6 sources
- Write the output to /tmp/highesttemp
- FanControl can then use it as the sensor source
