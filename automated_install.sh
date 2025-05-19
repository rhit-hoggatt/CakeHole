#!/bin/bash

# CakeHole Installation Script
# Designed to be run via: curl -sSL https://raw.githubusercontent.com/rhit-hoggatt/CakeHole/main/automated_install.sh | sudo bash

# --- Configuration ---
readonly PROJECT_NAME="cakehole"
readonly REPO_URL="https://github.com/rhit-hoggatt/CakeHole.git"

# Relative paths within your repository
readonly SERVER_BINARY_REL_PATH="releases/v1.0/server/server"
readonly WEB_SERVER_DIR_REL_PATH="web" # Assumes your Node.js server.js is in a 'web' subdirectory

# Installation directory
readonly INSTALL_BASE_DIR="/opt"
readonly INSTALL_DIR="$INSTALL_BASE_DIR/$PROJECT_NAME"

# Service names
readonly SERVICE_NAME_DNS="${PROJECT_NAME}_dns_server"
readonly SERVICE_NAME_WEB="${PROJECT_NAME}_web_server"

# Node.js version to install
readonly NODE_VERSION="22"

# --- Global Variables (will be set later) ---
NODE_EXEC_PATH="" # Will be set by install_node function
NPM_EXEC_PATH=""  # Will be set by install_node function

# --- Helper Functions ---

# Function to print messages
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

# Install essential dependencies like git and curl
install_base_dependencies() {
  info "Updating package lists and installing base dependencies (git, curl)..."
  if ! apt-get update -y; then
    error "Failed to update package lists. Check your internet connection and repository configuration."
    exit 1
  fi
  if ! apt-get install -y curl git; then
    error "Failed to install base dependencies (git, curl). Please install them manually and try again."
    exit 1
  fi
  info "Base dependencies installed successfully."
}

# Clone or update the repository
setup_repository() {
  info "Setting up repository in $INSTALL_DIR..."
  if [ -d "$INSTALL_DIR/.git" ]; then
    info "Existing installation found in $INSTALL_DIR. Attempting to update..."
    cd "$INSTALL_DIR" || { error "Failed to cd into $INSTALL_DIR"; exit 1; }
    if git pull; then
      info "Repository updated successfully."
    else
      error "Failed to update repository. Consider removing $INSTALL_DIR and re-running."
    fi
    # Ensure we cd back to the original directory if the script was not started from $INSTALL_DIR
    # Check if OLDPWD is set and not empty before trying to cd to it.
    if [ "$PWD" != "$(dirname "$0")" ] && [ -n "$OLDPWD" ] && [ "$OLDPWD" != "$PWD" ]; then
        cd "$OLDPWD" || { error "Failed to cd back to original directory from $PWD (OLDPWD: $OLDPWD)"; exit 1; }
    fi
  else
    info "Cloning CakeHole repository from $REPO_URL into $INSTALL_DIR..."
    mkdir -p "$(dirname "$INSTALL_DIR")" || { error "Failed to create parent directory for $INSTALL_DIR"; exit 1; }
    local temp_clone_dir
    temp_clone_dir=$(mktemp -d)
    if git clone --depth 1 "$REPO_URL" "$temp_clone_dir"; then
      mkdir -p "$INSTALL_DIR" || { error "Failed to create $INSTALL_DIR before rsync"; rm -rf "$temp_clone_dir"; exit 1; }
      if rsync -av --remove-source-files "$temp_clone_dir/" "$INSTALL_DIR/"; then
        info "Repository cloned successfully into $INSTALL_DIR."
      else
        error "Failed to move cloned files to $INSTALL_DIR."
        rm -rf "$temp_clone_dir"; exit 1
      fi
      rm -rf "$temp_clone_dir"
    else
      error "Failed to clone repository from $REPO_URL"
      rm -rf "$temp_clone_dir"; exit 1
    fi
  fi
}

# Install Node.js using NVM (Node Version Manager)
install_node() {
  info "Installing Node.js v$NODE_VERSION using NVM..."
  export NVM_DIR="$HOME/.nvm" 
  
  if curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash; then
    info "NVM installed or updated."
  else
    error "Failed to download or run NVM install script."
    return 1
  fi

  [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
  [ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"

  if ! command -v nvm &> /dev/null; then
    error "NVM command not found after installation. Sourcing NVM might have failed."
    return 1
  fi
  info "NVM command is available."
  
  if nvm install "$NODE_VERSION"; then
    info "Node.js v$NODE_VERSION installed successfully."
  else
    error "Failed to install Node.js v$NODE_VERSION using NVM."
    return 1
  fi
  
  nvm alias default "$NODE_VERSION"
  nvm use default

  NODE_EXEC_PATH=$(nvm_find_node_path "$NODE_VERSION")
  if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
      NODE_EXEC_PATH=$(command -v node) 
      if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
          NODE_EXEC_PATH=$(find "$NVM_DIR/versions/node" -path "*/v$NODE_VERSION.*/bin/node" -type f 2>/dev/null | head -n 1)
      fi
  fi

  if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
    error "Could not determine the absolute path to the Node.js v$NODE_VERSION executable."
    return 1
  fi
  info "Node.js executable path: $NODE_EXEC_PATH"
  "$NODE_EXEC_PATH" -v
  
  NPM_EXEC_PATH="$(dirname "$NODE_EXEC_PATH")/npm"
  if [ ! -f "$NPM_EXEC_PATH" ]; then
      error "Could not determine the absolute path to npm."
      NPM_EXEC_PATH="" # Ensure it's empty if not found
      return 1
  fi
  info "NPM executable path: $NPM_EXEC_PATH"
  "$NPM_EXEC_PATH" -v

  info "Node.js and npm are ready."
  return 0
}

# Create and enable systemd service for the DNS server
create_dns_service() {
  local server_binary_abs_path="$1"
  local dns_working_dir
  dns_working_dir=$(dirname "$server_binary_abs_path")

  if [ ! -f "$server_binary_abs_path" ]; then
    error "DNS server binary not found at $server_binary_abs_path"
    return 1
  fi
  if [ ! -x "$server_binary_abs_path" ]; then
    info "DNS server binary at $server_binary_abs_path is not executable. Attempting to chmod +x..."
    chmod +x "$server_binary_abs_path"
    if [ ! -x "$server_binary_abs_path" ]; then
        error "Failed to make DNS server binary executable. Please check permissions."
        return 1
    fi
  fi

  info "Creating DNS server service: $SERVICE_NAME_DNS"
  cat > "/etc/systemd/system/$SERVICE_NAME_DNS.service" <<EOF
[Unit]
Description=CakeHole DNS Server ($PROJECT_NAME)
After=network.target

[Service]
User=root
WorkingDirectory=$dns_working_dir
ExecStart=$server_binary_abs_path
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

  systemctl daemon-reload
  systemctl enable "$SERVICE_NAME_DNS"
  if systemctl start "$SERVICE_NAME_DNS"; then
    info "$SERVICE_NAME_DNS service started and enabled."
  else
    error "Failed to start $SERVICE_NAME_DNS service. Check 'systemctl status $SERVICE_NAME_DNS' and 'journalctl -u $SERVICE_NAME_DNS'."
    return 1
  fi
  return 0
}

# Create and enable systemd service for the Web server
create_web_service() {
  local web_server_abs_dir="$1" 
  local node_exec="$2"          

  if [ ! -d "$web_server_abs_dir" ]; then
    error "Web server directory not found at $web_server_abs_dir"
    return 1
  fi
  if [ ! -f "$web_server_abs_dir/server.js" ]; then 
    error "Web server main file 'server.js' not found in $web_server_abs_dir"
    return 1
  fi
  if [ -z "$node_exec" ] || [ ! -f "$node_exec" ]; then
    error "Node.js executable path is invalid or not provided for web service."
    return 1
  fi

  info "Creating Web server service: $SERVICE_NAME_WEB"
  # Note: Removed the trailing comment from ExecStart
  cat > "/etc/systemd/system/$SERVICE_NAME_WEB.service" <<EOF
[Unit]
Description=CakeHole Web Server ($PROJECT_NAME)
After=network.target $SERVICE_NAME_DNS.service

[Service]
User=root
WorkingDirectory=$web_server_abs_dir
ExecStart=$node_exec server.js
Restart=on-failure
RestartSec=5
# Environment=NODE_ENV=production # Optional: for production Node.js apps

[Install]
WantedBy=multi-user.target
EOF

  systemctl daemon-reload
  systemctl enable "$SERVICE_NAME_WEB"
  if systemctl start "$SERVICE_NAME_WEB"; then
    info "$SERVICE_NAME_WEB service started and enabled."
  else
    error "Failed to start $SERVICE_NAME_WEB service. Check 'systemctl status $SERVICE_NAME_WEB' and 'journalctl -u $SERVICE_NAME_WEB'."
    return 1
  fi
  return 0
}

# --- Main Script Execution ---
main() {
  check_root
  install_base_dependencies
  setup_repository

  local final_server_binary="$INSTALL_DIR/$SERVER_BINARY_REL_PATH"
  local final_web_server_dir="$INSTALL_DIR/$WEB_SERVER_DIR_REL_PATH"

  if [ "$PWD" != "$INSTALL_DIR" ]; then # Ensure current directory is INSTALL_DIR for relative path operations if any
    cd "$INSTALL_DIR" || { error "Failed to change directory to $INSTALL_DIR. Aborting."; exit 1; }
  fi

  if ! install_node; then 
    error "Node.js installation failed. Aborting."
    exit 1
  fi
  
  if [ -z "$NODE_EXEC_PATH" ] || [ -z "$NPM_EXEC_PATH" ]; then
      error "Global NODE_EXEC_PATH or NPM_EXEC_PATH was not set by install_node. Cannot create web service or install dependencies."
      exit 1
  fi

  # Install npm dependencies for the web server if package.json exists
  if [ -f "$final_web_server_dir/package.json" ]; then
    info "Found package.json in $final_web_server_dir. Running npm install..."
    # Ensure the web server directory exists before trying to cd into it
    if [ ! -d "$final_web_server_dir" ]; then
        error "Web server directory $final_web_server_dir does not exist. Cannot run npm install."
        exit 1 # Or handle error differently
    fi
    pushd "$final_web_server_dir" > /dev/null || { error "Failed to cd to $final_web_server_dir"; exit 1; }
    if "$NPM_EXEC_PATH" install; then
      info "npm install completed successfully in $final_web_server_dir."
    else
      error "npm install failed in $final_web_server_dir."
      popd > /dev/null
      # Decide if this is a fatal error
      # exit 1; 
    fi
    popd > /dev/null || { error "Failed to popd from $final_web_server_dir"; exit 1; }
  else
    info "No package.json found in $final_web_server_dir, skipping npm install."
  fi

  if ! create_dns_service "$final_server_binary"; then
    error "DNS service creation failed."
  fi

  if ! create_web_service "$final_web_server_dir" "$NODE_EXEC_PATH"; then
    error "Web service creation failed."
  fi

  info "--------------------------------------------------------------------"
  info "CakeHole ($PROJECT_NAME) installation attempt complete!"
  info "--------------------------------------------------------------------"
  # ... (rest of the info messages)
}

# Execute the main function
main

exit 0
