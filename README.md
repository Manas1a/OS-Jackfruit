Multi-Container Runtime with Kernel Memory Monitor

1. Team Information

Name	SRN

Manasa Bhaktha S	PES1UG24CS258

Mridhini M R	PES1UG24CS277

2. Project Overview

This project implements a lightweight container runtime in C that supports running multiple isolated containers simultaneously under a long-running supervisor process. The runtime uses Linux namespaces to provide process and filesystem isolation, and a kernel module to monitor container memory usage.

Key features include:

Multi-container supervision
Namespace-based isolation
Bounded-buffer logging system
Kernel memory monitoring with soft and hard limits
CLI interface for container management
Scheduling experiments using CPU, memory, and I/O workloads

The project demonstrates core operating system concepts including process lifecycle management, interprocess communication, synchronization, memory monitoring, and scheduling behavior.

3. Build, Load, and Run Instructions

The project was tested on **Ubuntu 22.04/24.04.

Install Dependencies
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
Build the Project

Navigate to the boilerplate directory:

cd boilerplate
make

This builds:

engine – container runtime and supervisor
monitor.ko – kernel memory monitor
cpu_hog, memory_hog, io_pulse – workload programs
Load the Kernel Module
sudo insmod monitor.ko

Verify the device:

ls -l /dev/container_monitor
Start the Supervisor
sudo ./engine supervisor ./rootfs-base

The supervisor process manages container creation and lifecycle.

Create Container Filesystems
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta

Each container requires its own writable filesystem.

Start Containers

In another terminal:

sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
sudo ./engine start beta ./rootfs-beta /bin/sh --soft-mib 64 --hard-mib 96
List Running Containers
sudo ./engine ps

This command shows container metadata including PID, name, and resource limits.

View Container Logs
sudo ./engine logs alpha
Run Workloads

CPU workload:

./cpu_hog

Memory workload:

./memory_hog

I/O workload:

./io_pulse

To run workloads inside containers, copy them into the container filesystem before launch:

cp memory_hog ./rootfs-alpha/
Stop Containers
sudo ./engine stop alpha
sudo ./engine stop beta
Inspect Kernel Logs
dmesg | tail

This shows soft-limit warnings and hard-limit enforcement events.

Unload Kernel Module
sudo rmmod monitor
4. Screenshots
Multi-container supervision

Shows two containers running under a single supervisor.

Container metadata tracking

Displays metadata such as PID, container name, and memory limits.

Bounded-buffer logging

Shows log messages processed through the logging pipeline.

CLI and IPC

Demonstrates communication between the CLI and the supervisor.

Soft memory limit warning

Kernel log showing a soft memory limit warning.

Hard limit enforcement

Kernel log showing container termination after exceeding the hard limit.

Scheduling experiment

Comparison of CPU-bound and I/O-bound workload behavior.

Clean teardown

Shows that containers exit cleanly without leaving zombie processes.

5. Engineering Analysis
Isolation Mechanisms

The runtime uses Linux namespaces to provide isolation between containers.

Three namespaces are used:

PID namespace
Each container has its own process ID space. Processes inside the container see PID 1 as the init process.

UTS namespace
Allows each container to have its own hostname.

Mount namespace
Provides filesystem isolation so containers see a different root filesystem.

Filesystem isolation is implemented using chroot or pivot_root, which changes the root directory visible to a process.

However, containers still share the same host kernel. This means the CPU scheduler, memory subsystem, and kernel modules remain shared across containers.

Supervisor and Process Lifecycle

The runtime uses a long-running supervisor process to manage containers.

The supervisor performs several responsibilities:

creating container processes
maintaining container metadata
tracking container state
reaping terminated containers

When a container is created, the supervisor calls clone() with namespace flags. The new process becomes the container’s init process.

The supervisor maintains parent-child relationships and uses waitpid() to reap exited processes, preventing zombie processes.

Signals are used to stop or terminate containers when necessary.

IPC, Threads, and Synchronization

The system uses multiple IPC mechanisms.

CLI communication with the supervisor occurs using pipes or sockets.
Communication with the kernel module occurs through ioctl calls.

The logging system uses a bounded-buffer design implemented with the producer-consumer model.

Producers generate log messages, while a consumer thread writes them to disk.

Without synchronization, race conditions could occur where multiple threads attempt to write to the buffer simultaneously, causing corrupted log data.

To prevent this, synchronization primitives such as mutexes and condition variables are used.

The bounded buffer prevents overflow by forcing producers to wait when the buffer is full, ensuring that no log messages are lost.

Memory Management and Enforcement

The kernel module monitors container memory usage using RSS (Resident Set Size).

RSS measures the amount of physical memory currently used by a process.

However, RSS does not include swapped memory or some shared memory mappings.

Two types of memory limits are implemented:

Soft limit
When exceeded, a warning message is generated but the container continues running.

Hard limit
When exceeded, the container process is terminated.

Memory enforcement must occur in kernel space because the kernel has direct access to process memory information and can enforce limits reliably.

User-space monitoring alone could be bypassed or delayed.

Scheduling Behavior

Linux uses a scheduling algorithm designed to balance fairness, responsiveness, and throughput.

During experiments:

CPU-bound workloads such as cpu_hog continuously consume CPU resources.

I/O-bound workloads such as io_pulse frequently block while waiting for I/O operations.

The scheduler prioritizes responsiveness by allowing I/O-bound processes to resume quickly after blocking.

These results demonstrate how Linux scheduling adapts dynamically to different workload types.

6. Design Decisions and Tradeoffs
Namespace Isolation

Choice: PID, UTS, and mount namespaces.
Tradeoff: network isolation was not implemented.
Justification: these namespaces provide sufficient process and filesystem isolation while keeping implementation manageable.

Supervisor Architecture

Choice: single long-running supervisor.
Tradeoff: central point of failure.
Justification: simplifies container lifecycle management and metadata tracking.

Logging System

Choice: bounded-buffer producer-consumer design.
Tradeoff: requires synchronization primitives.
Justification: prevents log corruption and improves logging efficiency.

Kernel Memory Monitor

Choice: memory monitoring implemented as a kernel module.
Tradeoff: increased implementation complexity.
Justification: kernel-space monitoring provides accurate and reliable memory enforcement.

7. Scheduler Experiment Results
Workload	CPU Usage	Observed Behavior
cpu_hog	High	Continuous CPU consumption
io_pulse	Low	Frequent blocking and rescheduling
memory_hog	Moderate	Memory-intensive workload

The results demonstrate how Linux scheduling balances CPU allocation between different workload types while maintaining fairness and responsiveness.
