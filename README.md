# CakeHole DNS Ad-Blocker

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) CakeHole is a DNS-based ad and tracker blocking server, similar to Pi-hole, designed to run on your local network. By routing your devices' DNS queries through CakeHole, you can block unwanted content at the network level, improving privacy and browsing speed across all your devices.

This project was developed as a final project for a computer networks class, aiming to provide a learning resource and a functional ad-blocking solution. I encourage you to use it, learn from it, and contribute to its development!

## Installation

* One line automated installation:
```bash
curl -sSL https://raw.githubusercontent.com/rhit-hoggatt/CakeHole/main/automated_install.sh | sudo bash
```
*The first login sets the credentials to be used for all subsequent logins

## Features

* **Network-wide Ad Blocking:** Blocks ads on all devices configured to use CakeHole as their DNS server.
* **Tracker Blocking:** Helps prevent tracking by known advertising and analytics domains.
* **Customizable Adlists:** Easily add new adlists or remove existing ones to tailor blocking to your needs.
* **Adlist Updates:** Keep your blocklists current with a built-in mechanism to update adlists.
* **Local DNS Records:** Define custom DNS entries for your local network (e.g., `my-nas.local` pointing to a local IP).
* **Configurable Performance:** Adjust the number of threads the server uses for processing DNS queries to optimize for your hardware.
* **Web Interface:** A user-friendly web UI on port `3333` to view statistics, manage settings, and monitor CakeHole's activity.
* **Lightweight:** Designed to be efficient and run on various Linux systems, including low-power devices like a Raspberry Pi.
* **Open Source:** Learn from the code, contribute, and adapt it to your needs.

## How it Works

CakeHole functions as a DNS sinkhole. When a device on your network attempts to access a domain:
1.  It queries CakeHole for the IP address.
2.  If the domain is found on one of the configured adlists (or matches a custom block rule), CakeHole responds with a non-routable IP address (e.g., `0.0.0.0`), effectively preventing your device from connecting to the unwanted server.
3.  If the domain is a custom local DNS entry, CakeHole responds with the configured local IP address.
4.  If the domain is not on any blocklist and not a local entry, CakeHole forwards the query to an upstream DNS resolver (e.g., Google, Cloudflare) and returns the legitimate IP address to your device.

## Prerequisites

* A Linux system (primarily tested on Debian/Ubuntu-based distributions).
* `systemd` init system.
* Root or `sudo` privileges for installation.
* An internet connection for downloading dependencies, adlists, and the repository.
* `git` and `curl` (the installer will attempt to install these if missing).
* Node.js v22 and npm (the installer will attempt to install these using NVM).
