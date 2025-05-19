#!/bin/bash

# CakeHole Installation Script

# --- Configuration ---
readonly PROJECT_NAME="cakehole"
readonly REPO_URL="https://github.com/rhit-hoggatt/CakeHole.git"

# Relative paths within your repository
readonly SERVER_BINARY_REL_PATH="releases/v1.0/server/server"
readonly WEB_SERVER_DIR_REL_PATH="web"

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
      # Optionally exit: exit 1
    fi
    # Ensure we cd back to the original directory if the script was not started from $INSTALL_DIR
    if [ "$PWD" != "$(dirname "$0")" ] && [ -n "$OLDPWD" ]; then
        cd "$OLDPWD" || { error "Failed to cd back to original directory"; exit 1; }
    fi
  else
    info "Cloning CakeHole repository from $REPO_URL into $INSTALL_DIR..."
    # Ensure parent directory exists
    mkdir -p "$(dirname "$INSTALL_DIR")" || { error "Failed to create parent directory for $INSTALL_DIR"; exit 1; }
    # Clone to a temporary name first to avoid issues if $INSTALL_DIR is not empty but not a git repo
    local temp_clone_dir
    temp_clone_dir=$(mktemp -d)
    if git clone --depth 1 "$REPO_URL" "$temp_clone_dir"; then
      # Move contents from temp clone dir to INSTALL_DIR
      # This handles the case where INSTALL_DIR might have other non-git files.
      # Using rsync is safer for merging.
      # Ensure INSTALL_DIR exists before rsyncing into it.
      mkdir -p "$INSTALL_DIR" || { error "Failed to create $INSTALL_DIR before rsync"; rm -rf "$temp_clone_dir"; exit 1; }
      if rsync -av --remove-source-files "$temp_clone_dir/" "$INSTALL_DIR/"; then
        info "Repository cloned successfully into $INSTALL_DIR."
      else
        error "Failed to move cloned files to $INSTALL_DIR."
        rm -rf "$temp_clone_dir"
        exit 1
      fi
      rm -rf "$temp_clone_dir"
    else
      error "Failed to clone repository from $REPO_URL"
      rm -rf "$temp_clone_dir"
      exit 1
    fi
  fi
}

# Install Node.js using NVM (Node Version Manager)
install_node() {
  info "Installing Node.js v$NODE_VERSION using NVM..."
  # NVM installation script might try to modify shell rc files.
  # We need to ensure NVM environment is sourced for *this script's* execution.
  # Since this script is run as root, NVM will be installed for the root user.
  export NVM_DIR="$HOME/.nvm" # Or /root/.nvm when run as sudo
  
  # Download and execute NVM install script
  if curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash; then # Using a specific stable version of NVM installer
    info "NVM installed."
  else
    error "Failed to download or run NVM install script."
    return 1 # Use return for functions, exit for script termination
  fi

  # Source NVM script to make nvm command available
  [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
  [ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"

  # Check if nvm command is available
  if ! command -v nvm &> /dev/null; then
    error "NVM command not found after installation. Sourcing NVM might have failed."
    error "Please try sourcing NVM manually in your shell and re-running parts of the script if needed:"
    error "  export NVM_DIR=\"\$([ -z \"\$XDG_CONFIG_HOME\" ] && printf %s \"\$HOME/.nvm\" || printf %s \"\$XDG_CONFIG_HOME/nvm\")\"" # More robust NVM_DIR
    error "  [ -s \"\$NVM_DIR/nvm.sh\" ] && \. \"\$NVM_DIR/nvm.sh\""
    return 1
  fi

  info "NVM command is available."
  
  # Install the specified Node.js version
  if nvm install "$NODE_VERSION"; then
    info "Node.js v$NODE_VERSION installed successfully."
  else
    error "Failed to install Node.js v$NODE_VERSION using NVM."
    return 1
  fi
  
  nvm alias default "$NODE_VERSION"
  nvm use default # Use the installed version

  # Get the absolute path to the installed Node executable
  NODE_EXEC_PATH=$(nvm_find_node_path "$NODE_VERSION") # nvm_find_node_path is an internal nvm function
  if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
      NODE_EXEC_PATH=$(command -v node) 
      if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
          NODE_EXEC_PATH=$(find "$NVM_DIR/versions/node" -path "*/v$NODE_VERSION.*/bin/node" -type f 2>/dev/null | head -n 1)
      fi
  fi

  if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
    error "Could not determine the absolute path to the Node.js v$NODE_VERSION executable."
    error "NVM_DIR is $NVM_DIR. Searched for node under versions/node/v$NODE_VERSION.*/bin/node"
    return 1
  fi
  info "Node.js executable path: $NODE_EXEC_PATH"
  
  # Verify Node and npm versions
  "$NODE_EXEC_PATH" -v
  local npm_path
  npm_path="$(dirname "$NODE_EXEC_PATH")/npm"
  "$npm_path" -v

  info "Node.js and npm are ready."

    # Go into the web server directory and run npm install
    local web_dir="$INSTALL_DIR/$WEB_SERVER_DIR_REL_PATH"
    if [ -d "$web_dir" ]; then
        info "Running npm install in $web_dir..."
        cd "$web_dir" || { error "Failed to cd into $web_dir"; return 1; }
        "$NODE_EXEC_PATH" "$(dirname "$NODE_EXEC_PATH")/npm" install
        if [ $? -eq 0 ]; then
            info "npm install completed successfully in $web_dir."
        else
            error "npm install failed in $web_dir."
            return 1
        fi
        cd - >/dev/null || true
    else
        error "Web directory $web_dir does not exist. Skipping npm install."
        return 1
    fi

  return 0
}

# Create and enable systemd service for the DNS server
create_dns_service() {
  local server_binary_abs_path="$1"
  local dns_working_dir
  dns_working_dir=$(dirname "$server_binary_abs_path")

  if [ ! -f "$server_binary_abs_path" ]; then
    error "DNS server binary not found at $server_binary_abs_path"
    error "Please ensure SERVER_BINARY_REL_PATH in the script correctly points to your server binary relative to the repository root."
    error "Expected structure: $INSTALL_DIR/$SERVER_BINARY_REL_PATH"
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
User=root # Or a dedicated user if your server supports it and files are accessible
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
    error "Failed to start $SERVICE_NAME_DNS service. Check status with 'systemctl status $SERVICE_NAME_DNS' and 'journalctl -u $SERVICE_NAME_DNS'."
    return 1
  fi
  return 0
}

# Create and enable systemd service for the Web server
create_web_service() {
  local web_server_abs_dir="$1" # e.g., /opt/cakehole/web
  local node_exec="$2"          # Absolute path to node executable

  if [ ! -d "$web_server_abs_dir" ]; then
    error "Web server directory not found at $web_server_abs_dir"
    return 1
  fi
  if [ ! -f "$web_server_abs_dir/server.js" ]; then # Assuming your main file is server.js
    error "Web server main file 'server.js' not found in $web_server_abs_dir"
    return 1
  fi
  if [ -z "$node_exec" ] || [ ! -f "$node_exec" ]; then
    error "Node.js executable path is invalid or not provided for web service."
    return 1
  fi

  info "Creating Web server service: $SERVICE_NAME_WEB"
  cat > "/etc/systemd/system/$SERVICE_NAME_WEB.service" <<EOF
[Unit]
Description=CakeHole Web Server ($PROJECT_NAME)
After=network.target $SERVICE_NAME_DNS.service # Optionally make it depend on DNS server

[Service]
User=root # Or a dedicated user. Ensure this user can access NVM's node if installed in root's home.
WorkingDirectory=$web_server_abs_dir
ExecStart=$node_exec server.js # Assumes main file is server.js
Restart=on-failure
RestartSec=5
# Environment=NODE_ENV=production # Optional: set environment

[Install]
WantedBy=multi-user.target
EOF

  systemctl daemon-reload
  systemctl enable "$SERVICE_NAME_WEB"
  if systemctl start "$SERVICE_NAME_WEB"; then
    info "$SERVICE_NAME_WEB service started and enabled."
  else
    error "Failed to start $SERVICE_NAME_WEB service. Check status with 'systemctl status $SERVICE_NAME_WEB' and 'journalctl -u $SERVICE_NAME_WEB'."
    return 1
  fi
  return 0
}

# --- Main Script Execution ---
main() {
  check_root
  install_base_dependencies
  setup_repository

  # Define absolute paths after repository setup
  local final_server_binary="$INSTALL_DIR/$SERVER_BINARY_REL_PATH"
  local final_web_server_dir="$INSTALL_DIR/$WEB_SERVER_DIR_REL_PATH"

  # Change to the installation directory for context if any sub-scripts need it
  # Though for service files, absolute paths are already used.
  # This cd is important if setup_repository did a git pull and OLDPWD was not set correctly
  # (e.g. if the script was executed directly from /)
  if [ "$PWD" != "$INSTALL_DIR" ]; then
    cd "$INSTALL_DIR" || { error "Failed to change directory to $INSTALL_DIR. Aborting."; exit 1; }
  fi


  if ! install_node; then # install_node sets global NODE_EXEC_PATH
    error "Node.js installation failed. Aborting."
    exit 1
  fi
  
  # Check if NODE_EXEC_PATH was set successfully
  if [ -z "$NODE_EXEC_PATH" ]; then
      error "Global NODE_EXEC_PATH was not set by install_node. Cannot create web service."
      exit 1
  fi

  if ! create_dns_service "$final_server_binary"; then
    error "DNS service creation failed."
    # Decide if you want to exit or continue to web service setup
  fi

  if ! create_web_service "$final_web_server_dir" "$NODE_EXEC_PATH"; then
    error "Web service creation failed."
  fi

  info "--------------------------------------------------------------------"
  info "CakeHole ($PROJECT_NAME) installation attempt complete!"
  info "--------------------------------------------------------------------"
  info "DNS Server IP: Use the IP address of this machine."
  info "   - Find with: ip addr show"
  info "Configure your router or devices to use this IP as their DNS server."
  info ""
  info "To check service status:"
  info "  sudo systemctl status $SERVICE_NAME_DNS"
  info "  sudo systemctl status $SERVICE_NAME_WEB"
  info ""
  info "To view logs:"
  info "  sudo journalctl -u $SERVICE_NAME_DNS -f"
  info "  sudo journalctl -u $SERVICE_NAME_WEB -f"
  info ""
  info "If you have a web interface, it might be accessible at http://<your_server_ip>:3333"
  info "Default web server directory: $final_web_server_dir"
  info "--------------------------------------------------------------------"
}

# Execute the main function
main

exit 0