#!/bin/bash

# CakeHole Uninstallation Script
# This script will stop and disable CakeHole services,
# remove the application files from /opt/cakehole,
# and remove the systemd service files.

# --- Configuration (should match your installation script) ---
readonly PROJECT_NAME="cakehole"
readonly INSTALL_DIR="/opt/$PROJECT_NAME"
readonly SERVICE_NAME_DNS="${PROJECT_NAME}_dns_server"
readonly SERVICE_NAME_WEB="${PROJECT_NAME}_web_server"

# --- Helper Functions ---
info() {
  echo "[INFO] $1"
}

error() {
  echo "[ERROR] $1" >&2
}

# Check for root privileges
check_root() {
  if [[ $EUID -ne 0 ]]; then
    error "This script requires root privileges. Please run with sudo or as root."
    exit 1
  fi
  info "Root privileges confirmed."
}

# --- Main Uninstallation Logic ---
main() {
  check_root

  info "Starting CakeHole Uninstallation..."

  # 1. Stop and disable services
  info "Stopping and disabling systemd services..."
  if systemctl is-active --quiet "$SERVICE_NAME_DNS"; then
    systemctl stop "$SERVICE_NAME_DNS"
    info "$SERVICE_NAME_DNS service stopped."
  else
    info "$SERVICE_NAME_DNS service was not active."
  fi
  if systemctl is-enabled --quiet "$SERVICE_NAME_DNS"; then
    systemctl disable "$SERVICE_NAME_DNS"
    info "$SERVICE_NAME_DNS service disabled."
  else
    info "$SERVICE_NAME_DNS service was not enabled."
  fi

  if systemctl is-active --quiet "$SERVICE_NAME_WEB"; then
    systemctl stop "$SERVICE_NAME_WEB"
    info "$SERVICE_NAME_WEB service stopped."
  else
    info "$SERVICE_NAME_WEB service was not active."
  fi
  if systemctl is-enabled --quiet "$SERVICE_NAME_WEB"; then
    systemctl disable "$SERVICE_NAME_WEB"
    info "$SERVICE_NAME_WEB service disabled."
  else
    info "$SERVICE_NAME_WEB service was not enabled."
  fi

  # 2. Remove systemd service files
  local service_file_dns="/etc/systemd/system/$SERVICE_NAME_DNS.service"
  local service_file_web="/etc/systemd/system/$SERVICE_NAME_WEB.service"

  if [ -f "$service_file_dns" ]; then
    info "Removing DNS service file: $service_file_dns"
    rm -f "$service_file_dns"
  else
    info "DNS service file not found: $service_file_dns"
  fi

  if [ -f "$service_file_web" ]; then
    info "Removing Web service file: $service_file_web"
    rm -f "$service_file_web"
  else
    info "Web service file not found: $service_file_web"
  fi

  # 3. Reload systemd daemon
  info "Reloading systemd daemon..."
  systemctl daemon-reload
  systemctl reset-failed # Clears any failed state for the services

  # 4. Remove the application directory
  if [ -d "$INSTALL_DIR" ]; then
    info "Removing application directory: $INSTALL_DIR"
    rm -rf "$INSTALL_DIR"
    if [ $? -eq 0 ]; then
      info "Successfully removed $INSTALL_DIR."
    else
      error "Failed to remove $INSTALL_DIR. Please check permissions or remove manually."
    fi
  else
    info "Application directory not found: $INSTALL_DIR"
  fi

  # 5. Disable and clean up UFW if it was set up by CakeHole
  if command -v ufw &> /dev/null; then
    info "Cleaning up UFW firewall rules..."

    # Remove CakeHole-specific rules
    ufw delete allow 53/udp 2>/dev/null && info "Removed UFW rule for port 53/udp (DNS)." || true
    ufw delete allow 67/udp 2>/dev/null && info "Removed UFW rule for port 67/udp (DHCP)." || true
    ufw delete allow 3333/tcp 2>/dev/null && info "Removed UFW rule for port 3333/tcp (Web UI)." || true
    ufw delete allow out 3333/tcp 2>/dev/null && info "Removed UFW outbound rule for port 3333/tcp." || true

    # Disable UFW to restore connectivity
    info "Disabling UFW..."
    if ufw --force disable; then
      info "UFW disabled successfully."
    else
      error "Failed to disable UFW. You may need to run 'sudo ufw disable' manually."
    fi

    # Reset UFW to factory defaults
    info "Resetting UFW to default state..."
    if ufw --force reset; then
      info "UFW reset to defaults."
    else
      error "Failed to reset UFW. You may need to run 'sudo ufw --force reset' manually."
    fi
  else
    info "UFW is not installed. Skipping firewall cleanup."
  fi

  info "--------------------------------------------------------------------"
  info "CakeHole ($PROJECT_NAME) uninstallation attempt complete."
  info "--------------------------------------------------------------------"
  info "Please manually remove any other dependencies if they are no longer needed:"
  info " - git, curl, libmicrohttpd-dev, libldns-dev"
  info " - Node.js and NVM (if installed by the script for the root user):"
  info "   To remove NVM (Node Version Manager) for the root user:"
  info "     sudo rm -rf /root/.nvm"
  info "   You might also need to remove NVM lines from /root/.bashrc if any were added."
  info "--------------------------------------------------------------------"
  info "Remember to revert any DNS changes on your router or devices."
  info "--------------------------------------------------------------------"

}

# Execute the main function
main

exit 0
