#!/bin/bash

# Script to install and configure DNS and Web Servers

# --- Configuration ---
REPO_DIR="$PWD"
SERVER_BINARY="$REPO_DIR/releases/server" 
WEB_SERVER_DIR="$WEB_SERVER_DIR" 
SERVICE_NAME_DNS="dns_server"
SERVICE_NAME_WEB="web_server"

# --- Helper Functions ---

function check_root() {
  if [[ $EUID -ne 0 ]]; then
    echo "This script requires root privileges.  Please run with sudo."
    exit 1
  fi
}

function install_dependencies() {
  echo "Installing dependencies..."
  apt-get update
  apt-get install -y curl git
}

function install_node() {
  echo "Installing Node.js..."
  curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
  source "$HOME/.nvm/nvm.sh"
  nvm install 22
  nvm current
  node -v
  npm -v
}

function create_dns_service() {
  echo "Creating DNS server service..."
  cat > /etc/systemd/system/$SERVICE_NAME_DNS.service <<EOF
[Unit]
Description=CakeHole
After=network.target

[Service]
User=root  # Or a dedicated user
WorkingDirectory=$REPO_DIR/releases
ExecStart=$SERVER_BINARY
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF
  systemctl daemon-reload
  systemctl enable $SERVICE_NAME_DNS
  systemctl start $SERVICE_NAME_DNS
}

function create_web_service() {
  echo "Creating Web server service..."
  cat > /etc/systemd/system/$SERVICE_NAME_WEB.service <<EOF
[Unit]
Description=Web Server
After=network.target

[Service]
User=root  # Or a dedicated user
WorkingDirectory=$WEB_SERVER_DIR
ExecStart=node server.js
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF
  systemctl daemon-reload
  systemctl enable $SERVICE_NAME_WEB
  systemctl start $SERVICE_NAME_WEB
}

# --- Main Script ---

check_root

install_dependencies
install_node

create_dns_service
create_web_service

echo "Installation and service creation complete!"