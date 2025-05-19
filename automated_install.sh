#!/bin/bash

# CakeHole Universal Installation Script
# Installs C dependencies, Node.js via NVM, fetches the repository,
# detects architecture for the C server binary, and sets up systemd services.
# Designed to be run via: curl -sSL https://raw.githubusercontent.com/rhit-hoggatt/CakeHole/main/automated_install.sh | sudo bash

# --- Configuration ---
readonly PROJECT_NAME="cakehole"
readonly REPO_URL="https://github.com/rhit-hoggatt/CakeHole.git" # Your repository URL

# Base path for server binaries - architecture will be appended
readonly SERVER_BINARY_BASE_PATH="releases/v1.0" # e.g., releases/v1.0/x86/server/server
readonly WEB_SERVER_DIR_REL_PATH="web" # Assumes your Node.js server.js is in a 'web' subdirectory

# Installation directory
readonly INSTALL_BASE_DIR="/opt"
readonly INSTALL_DIR="$INSTALL_BASE_DIR/$PROJECT_NAME"

# Service names
readonly SERVICE_NAME_DNS="${PROJECT_NAME}_dns_server"
readonly SERVICE_NAME_WEB="${PROJECT_NAME}_web_server"

# Node.js version to install via NVM
readonly NODE_VERSION="22"

# --- Global Variables ---
NODE_EXEC_PATH=""
NPM_EXEC_PATH=""
SERVER_BINARY_REL_PATH="" # Will be set based on architecture

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

# Detect system architecture and set server binary path
set_architecture_specific_paths() {
  local arch
  arch=$(uname -m)
  info "Detected system architecture: $arch"

  if [[ "$arch" == "x86_64" ]]; then
    SERVER_BINARY_REL_PATH="$SERVER_BINARY_BASE_PATH/x86/server/server"
  elif [[ "$arch" == "armv7l" || "$arch" == "armhf" || "$arch" == "aarch64" || "$arch" == "arm64" ]]; then
    # Consolidating common ARM architectures. Ensure your 'arm' binary supports these.
    # If you have separate armhf vs aarch64 binaries, you'll need more specific checks.
    SERVER_BINARY_REL_PATH="$SERVER_BINARY_BASE_PATH/arm/server/server"
  else
    error "Unsupported architecture: $arch. Cannot determine C server binary path."
    error "Please check if a pre-compiled binary is available for your system or consider compiling from source."
    exit 1
  fi
  info "C server binary relative path set to: $SERVER_BINARY_REL_PATH"
}


# Install essential base dependencies including C libraries, git, and curl
install_base_dependencies() {
  info "Checking and installing base dependencies (git, curl, C libraries)..."
  local packages_to_install=()
  
  # Check for git
  if ! command -v git &> /dev/null; then
    packages_to_install+=("git")
  fi
  
  # Check for curl
  if ! command -v curl &> /dev/null; then
    packages_to_install+=("curl")
  fi

  # Add C library development packages
  if ! dpkg -s libmicrohttpd-dev &> /dev/null; then
    packages_to_install+=("libmicrohttpd-dev")
  fi
  if ! dpkg -s libldns-dev &> /dev/null; then
    packages_to_install+=("libldns-dev")
  fi

  if [ ${#packages_to_install[@]} -gt 0 ]; then
    info "Updating package lists..."
    if ! apt-get update -y; then
      error "Failed to update package lists. Check your internet connection and repository configuration."
    fi
    info "Attempting to install missing dependencies: ${packages_to_install[*]}"
    local all_successful=true
    for pkg in "${packages_to_install[@]}"; do
        info "Installing $pkg..."
        if ! apt-get install -y "$pkg"; then
            error "Failed to install $pkg. Please check for errors and try installing it manually."
            all_successful=false
        else
            info "$pkg installed successfully."
        fi
    done

    if ! $all_successful; then
        error "One or more base dependencies failed to install. The C server might not run correctly."
    else
        info "All attempted base dependencies installed successfully or were already present."
    fi
  else
    info "Base dependencies (git, curl, libmicrohttpd-dev, libldns-dev) appear to be already installed."
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
  info "Node.js executable path set to: $NODE_EXEC_PATH"
  
  NPM_EXEC_PATH="$(dirname "$NODE_EXEC_PATH")/npm"
  if [ ! -f "$NPM_EXEC_PATH" ]; then
      error "Could not determine the absolute path to npm. Expected at $(dirname "$NODE_EXEC_PATH")/npm"
      NPM_EXEC_PATH="" 
      return 1
  fi
  info "NPM executable path set to: $NPM_EXEC_PATH"
  
  "$NODE_EXEC_PATH" -v
  "$NPM_EXEC_PATH" -v

  info "Node.js and npm are ready."
  return 0
}


# Clone or update the repository
setup_repository() {
  info "Setting up repository in $INSTALL_DIR..."
  if [ -d "$INSTALL_DIR/.git" ]; then
    info "Existing installation found in $INSTALL_DIR. Attempting to update..."
    cd "$INSTALL_DIR" || { error "Failed to cd into $INSTALL_DIR"; exit 1; }
    if ! git diff --quiet || ! git diff --cached --quiet; then
        if git stash push -u -m "cakehole-setup-script-stash-$(date +%s)"; then
            info "Stashed local changes."
        else
            info "No local changes to stash or failed to stash."
        fi
    else
        info "No local changes to stash."
    fi

    if git pull --rebase; then 
      info "Repository updated successfully via rebase."
    else
      error "Rebase pull failed. Trying git pull without rebase..."
      if git pull; then
          info "Repository updated successfully (with merge)."
      else
          error "Failed to update repository even with a standard pull. Please check $INSTALL_DIR for conflicts."
          if git stash list | grep -q "cakehole-setup-script-stash"; then
              git stash pop || info "No stash to pop or stash pop failed after failed pull."
          fi
          exit 1 
      fi
    fi
    if git stash list | grep -q "cakehole-setup-script-stash"; then
        git stash pop || info "Stash pop failed after successful pull, resolve manually if needed."
    fi

    if [ "$PWD" != "$(dirname "$0")" ] && [ -n "$OLDPWD" ] && [ "$OLDPWD" != "$PWD" ]; then 
        cd "$OLDPWD" || { error "Failed to cd back to original directory from $PWD (OLDPWD: $OLDPWD)"; exit 1; }
    fi
  else
    info "Cloning CakeHole repository from $REPO_URL into $INSTALL_DIR..."
    mkdir -p "$(dirname "$INSTALL_DIR")" || { error "Failed to create parent directory for $INSTALL_DIR"; exit 1; }
    if git clone --depth 1 "$REPO_URL" "$INSTALL_DIR"; then
      info "Repository cloned successfully into $INSTALL_DIR."
    else
      error "Failed to clone repository from $REPO_URL into $INSTALL_DIR"
      exit 1
    fi
  fi
}

# Create and enable systemd service for the DNS server
create_dns_service() {
  # SERVER_BINARY_REL_PATH is now global and set by set_architecture_specific_paths
  local server_binary_abs_path="$INSTALL_DIR/$SERVER_BINARY_REL_PATH"
  local dns_working_dir
  dns_working_dir=$(dirname "$server_binary_abs_path") 

  if [ ! -f "$server_binary_abs_path" ]; then
    error "DNS server binary not found at $server_binary_abs_path"
    error "This could be due to an unsupported architecture or an issue with the repository structure."
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
  if systemctl restart "$SERVICE_NAME_DNS"; then 
    info "$SERVICE_NAME_DNS service (re)started and enabled."
  else
    error "Failed to (re)start $SERVICE_NAME_DNS service. Check 'systemctl status $SERVICE_NAME_DNS' and 'journalctl -u $SERVICE_NAME_DNS'."
    return 1
  fi
  return 0
}

# Create and enable systemd service for the Web server
create_web_service() {
  local web_server_abs_dir="$1" 

  if [ ! -d "$web_server_abs_dir" ]; then
    error "Web server directory not found at $web_server_abs_dir"
    return 1
  fi
  if [ ! -f "$web_server_abs_dir/server.js" ]; then 
    error "Web server main file 'server.js' not found in $web_server_abs_dir"
    return 1
  fi
  if [ -z "$NODE_EXEC_PATH" ] || [ ! -x "$NODE_EXEC_PATH" ]; then 
    error "Node.js executable path '$NODE_EXEC_PATH' is invalid or not executable. Was install_node successful?"
    return 1
  fi

  info "Creating Web server service: $SERVICE_NAME_WEB"
  cat > "/etc/systemd/system/$SERVICE_NAME_WEB.service" <<EOF
[Unit]
Description=CakeHole Web Server ($PROJECT_NAME)
After=network.target $SERVICE_NAME_DNS.service

[Service]
User=root
WorkingDirectory=$web_server_abs_dir
ExecStart=$NODE_EXEC_PATH server.js
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

  systemctl daemon-reload
  systemctl enable "$SERVICE_NAME_WEB"
  if systemctl restart "$SERVICE_NAME_WEB"; then 
    info "$SERVICE_NAME_WEB service (re)started and enabled."
  else
    error "Failed to (re)start $SERVICE_NAME_WEB service. Check 'systemctl status $SERVICE_NAME_WEB' and 'journalctl -u $SERVICE_NAME_WEB'."
    return 1
  fi
  return 0
}

# --- Main Script Execution ---
main() {
  check_root
  set_architecture_specific_paths # Determine SERVER_BINARY_REL_PATH early
  
  install_base_dependencies 

  if ! install_node; then 
    error "Node.js installation failed. Aborting."
    exit 1
  fi
  
  if [ -z "$NODE_EXEC_PATH" ] || [ -z "$NPM_EXEC_PATH" ]; then
      error "NODE_EXEC_PATH or NPM_EXEC_PATH was not set by install_node. Aborting."
      exit 1
  fi

  setup_repository
  
  # SERVER_BINARY_REL_PATH is now set globally by set_architecture_specific_paths
  local final_server_binary_path="$INSTALL_DIR/$SERVER_BINARY_REL_PATH" 
  local final_web_server_dir="$INSTALL_DIR/$WEB_SERVER_DIR_REL_PATH"
  
  local c_server_working_dir
  c_server_working_dir=$(dirname "$final_server_binary_path")

  local adlist_data_path="$c_server_working_dir/adlists/listdata"
  info "Ensuring adlist data directory exists: $adlist_data_path"
  if mkdir -p "$adlist_data_path"; then
    info "Directory $adlist_data_path ensured."
  else
    error "Failed to create directory $adlist_data_path. The C server might fail."
  fi
  local adlist_metadata_path="$c_server_working_dir/adlists/metadata"
  info "Ensuring adlist metadata directory exists: $adlist_metadata_path"
  if mkdir -p "$adlist_metadata_path"; then
      info "Directory $adlist_metadata_path ensured."
  else
      error "Failed to create directory $adlist_metadata_path."
  fi

  # Ensure the parent directory for the user data file exists if it's different
  # Example: if password file is in $c_server_working_dir/userdata/password.txt
  local user_data_parent_dir="$c_server_working_dir/userdata" # ADJUST THIS PATH if your password file is in a subdir
  info "Ensuring user data parent directory exists: $user_data_parent_dir"
  if mkdir -p "$user_data_parent_dir"; then
      info "Directory $user_data_parent_dir ensured."
  else
      error "Failed to create directory $user_data_parent_dir. Password file operations might fail."
  fi


  if [ "$PWD" != "$INSTALL_DIR" ]; then
    cd "$INSTALL_DIR" || { error "Failed to change directory to $INSTALL_DIR. Aborting."; exit 1; }
  fi
  
  if [ -f "$final_web_server_dir/package.json" ]; then
    info "Found package.json in $final_web_server_dir. Running npm install..."
    if [ ! -d "$final_web_server_dir" ]; then
        error "Web server directory $final_web_server_dir does not exist. Cannot run npm install."
    else
        pushd "$final_web_server_dir" > /dev/null || { error "Failed to cd to $final_web_server_dir"; exit 1; }
        if "$NPM_EXEC_PATH" install --omit=dev; then 
          info "npm install completed successfully in $final_web_server_dir."
        else
          error "npm install failed in $final_web_server_dir. Check for errors above."
        fi
        popd > /dev/null || { error "Failed to popd from $final_web_server_dir"; exit 1; }
    fi
  else
    info "No package.json found in $final_web_server_dir, skipping npm install."
  fi

  # Pass the dynamically determined absolute binary path to create_dns_service
  if ! create_dns_service; then # No longer needs argument, uses global SERVER_BINARY_REL_PATH
    error "DNS service creation failed. Please check logs."
  fi

  if ! create_web_service "$final_web_server_dir"; then 
    error "Web service creation failed. Please check logs."
  fi

  info "--------------------------------------------------------------------"
  info "CakeHole ($PROJECT_NAME) setup and service installation complete!"
  info "--------------------------------------------------------------------"
  # ... (rest of the info messages from previous script)
  info "Services should now be enabled and (re)started."
  info "To check service status:"
  info "  sudo systemctl status $SERVICE_NAME_DNS"
  info "  sudo systemctl status $SERVICE_NAME_WEB"
  info ""
  info "To view logs:"
  info "  sudo journalctl -u $SERVICE_NAME_DNS -f"
  info "  sudo journalctl -u $SERVICE_NAME_WEB -f"
  info ""
  info "Web UI should be at: http://<YOUR_SERVER_IP>:3333"
  info "--------------------------------------------------------------------"
}

# Execute the main function
main

exit 0
