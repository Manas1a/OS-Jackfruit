# Multi-Container Runtime with Kernel Memory Monitor
=======
Multi-Container Runtime with Kernel Memory Monitor

## Team Information

| Name | SRN |
|-----|-----|
| Manasa Bhaktha S | PES1UG24CS258 |
| Mridhini M R | PES1UG24CS277 |

---

# Project Overview

This project implements a lightweight container runtime in C that supports running multiple isolated containers simultaneously under a long-running supervisor process. The runtime uses Linux namespaces to provide process and filesystem isolation and a kernel module to monitor container memory usage.

### Key Features

- Multi-container supervision
- Namespace-based isolation
- Bounded-buffer logging system
- Kernel memory monitoring with soft and hard limits
- CLI interface for container management
- Scheduling experiments using CPU, memory, and I/O workloads

The project demonstrates core operating system concepts including:

- process lifecycle management  
- interprocess communication  
- synchronization mechanisms  
- memory monitoring and enforcement  
- scheduling behavior in Linux systems

---

# Build, Load, and Run Instructions

This project was tested on **Ubuntu 22.04 / 24.04**.

---

## Install Dependencies

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

---

## Build the Project

Navigate to the boilerplate directory and build the project:

```bash
cd boilerplate
make
```

This builds:

- `engine` – container runtime and supervisor
- `monitor.ko` – kernel memory monitor module
- `cpu_hog`, `memory_hog`, `io_pulse` – workload programs

---

## Load the Kernel Module

```bash
sudo insmod monitor.ko
```

Verify the device was created:

```bash
ls -l /dev/container_monitor
```

---

## Start the Supervisor

```bash
sudo ./engine supervisor ./rootfs-base
```

The supervisor process manages container lifecycle and metadata tracking.

---

## Create Container Root Filesystems

```bash
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta
```

Each container requires its own writable root filesystem.

---

## Start Containers

In another terminal window run:

```bash
sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
```

```bash
sudo ./engine start beta ./rootfs-beta /bin/sh --soft-mib 64 --hard-mib 96
```

---

## List Running Containers

```bash
sudo ./engine ps
```

This command displays container metadata including:

- container name
- PID
- memory limits
- container state

---

## View Container Logs

```bash
sudo ./engine logs alpha
```

---

## Run Workloads

CPU workload:

```bash
./cpu_hog
```

Memory workload:

```bash
./memory_hog
```

I/O workload:

```bash
./io_pulse
```

To run workloads inside containers, copy them into the container filesystem before launching:

```bash
cp memory_hog ./rootfs-alpha/
```

---

## Stop Containers

```bash
sudo ./engine stop alpha
sudo ./engine stop beta
```

---

## Inspect Kernel Logs

```bash
dmesg | tail
```

This command shows memory monitoring events including soft-limit warnings and hard-limit enforcement.

---

## Unload the Kernel Module

```bash
sudo rmmod monitor
```

---

# Screenshots

## Multi-Container Supervision

![Multi Container](screenshots/multi-container.png)

Shows two containers running under a single supervisor process.

---

## Container Metadata Tracking

![Container PS](screenshots/container-ps.png)

Shows container metadata including PID, name, and memory limits.

---

## Bounded Buffer Logging

![Logging](screenshots/logging.png)

Shows log messages processed through the logging pipeline.

---

## CLI and IPC Communication

![CLI IPC](screenshots/ipc-cli.png)

Shows CLI commands interacting with the supervisor.

---

## Soft Memory Limit Warning

![Soft Limit](screenshots/soft-limit.png)

Shows kernel log warning when container exceeds soft memory limit.

---

## Hard Memory Limit Enforcement

![Hard Limit](screenshots/hard-limit.png)

Shows container termination when memory usage exceeds the hard limit.

---

## Scheduling Experiment

![Scheduling](screenshots/scheduling.png)

Shows behavior differences between CPU-bound and I/O-bound workloads.

---

## Clean Teardown

![Cleanup](screenshots/cleanup.png)

Shows containers exiting cleanly without zombie processes.

---

# Engineering Analysis

## Isolation Mechanisms

Container isolation is implemented using Linux namespaces.

### PID Namespace
Provides each container with its own process ID space.

### UTS Namespace
Allows each container to have its own hostname.

### Mount Namespace
Provides filesystem isolation so containers see their own filesystem hierarchy.

Filesystem isolation is implemented using `chroot` or `pivot_root`, which changes the root directory visible to the container process.

However, containers still share the host kernel, meaning the CPU scheduler, memory subsystem, and kernel modules remain shared.

---

## Supervisor and Process Lifecycle

The runtime uses a long-running supervisor process to manage containers.

The supervisor is responsible for:

- creating container processes
- maintaining metadata about running containers
- handling container lifecycle events
- reaping terminated child processes

Containers are created using the `clone()` system call with namespace flags.  
The supervisor uses `waitpid()` to prevent zombie processes and to track container termination.

---

## IPC, Threads, and Synchronization

The system uses multiple IPC mechanisms.

Examples include:

- CLI communication with the supervisor using pipes or sockets
- kernel module communication through ioctl calls

The logging system uses a bounded buffer implemented using the producer-consumer model.

Producers generate log messages while a consumer thread writes them to disk.

Without synchronization, race conditions could occur where multiple threads write to the buffer simultaneously. To prevent this, mutexes and condition variables are used.

This design prevents lost log messages, data corruption, and deadlock conditions.

---

## Memory Management and Enforcement

The kernel module monitors memory usage using RSS (Resident Set Size).

RSS measures the amount of physical memory currently used by a process.

However, RSS does not include swapped memory or certain shared memory mappings.

Two types of limits are enforced:

### Soft Limit
Triggers a warning but allows the container to continue running.

### Hard Limit
Terminates the container process when exceeded.

Enforcement is implemented in kernel space because the kernel has direct access to process memory information and can enforce limits reliably.

---

## Scheduling Behavior

Linux scheduling attempts to balance fairness, responsiveness, and throughput.

During experiments:

- CPU-bound workloads such as `cpu_hog` continuously consume CPU resources.
- I/O-bound workloads such as `io_pulse` frequently block waiting for I/O.

The scheduler prioritizes responsiveness by allowing I/O-bound tasks to resume quickly after blocking.

These experiments demonstrate how Linux dynamically adjusts scheduling behavior based on workload characteristics.

---

# Design Decisions and Tradeoffs

### Namespace Isolation
Choice: Use PID, UTS, and mount namespaces.  
Tradeoff: Network namespace was not implemented.  
Justification: These namespaces provide sufficient isolation for this project.

### Supervisor Architecture
Choice: Single long-running supervisor.  
Tradeoff: Central point of failure.  
Justification: Simplifies container lifecycle management.

### Logging System
Choice: Bounded-buffer producer-consumer logging system.  
Tradeoff: Requires synchronization primitives.  
Justification: Prevents log corruption and improves throughput.

### Kernel Memory Monitor
Choice: Implemented in kernel space.  
Tradeoff: Increased implementation complexity.  
Justification: Allows reliable memory enforcement.

---

# Scheduler Experiment Results

| Workload | CPU Usage | Observed Behavior |
|--------|--------|--------|
| cpu_hog | High | Continuous CPU usage |
| io_pulse | Low | Frequent blocking and rescheduling |
| memory_hog | Moderate | Memory intensive workload |

The results show how the Linux scheduler balances CPU time among different workload types while maintaining fairness and responsiveness.
