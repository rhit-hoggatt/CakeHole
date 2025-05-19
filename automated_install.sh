#!/bin/bash

# CakeHole Service Setup Script
# Installs Node.js via NVM, fetches the repository, and sets up systemd services.
# Designed to be run via: curl -sSL <URL_TO_THIS_SCRIPT> | sudo bash

# --- Configuration ---
readonly PROJECT_NAME="cakehole"
readonly REPO_URL="https://github.com/rhit-hoggatt/CakeHole.git" # Your repository URL

# Relative paths within your repository
readonly SERVER_BINARY_REL_PATH="releases/v1.0/server/server"
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

# Install essential base dependencies like git and curl
install_base_dependencies() {
  info "Checking and installing base dependencies (git, curl)..."
  local packages_to_install=()
  if ! command -v git &> /dev/null; then
    packages_to_install+=("git")
  fi
  if ! command -v curl &> /dev/null; then
    packages_to_install+=("curl")
  fi

  if [ ${#packages_to_install[@]} -gt 0 ]; then
    info "Updating package lists..."
    if ! apt-get update -y; then
      error "Failed to update package lists. Check your internet connection and repository configuration."
      exit 1
    fi
    info "Installing missing dependencies: ${packages_to_install[*]}"
    if ! apt-get install -y "${packages_to_install[@]}"; then
      error "Failed to install base dependencies. Please install them manually and try again."
      exit 1
    fi
    info "Base dependencies installed successfully."
  else
    info "Base dependencies (git, curl) are already installed."
  fi
}

# Install Node.js using NVM (Node Version Manager)
install_node() {
  info "Installing Node.js v$NODE_VERSION using NVM..."
  # NVM installation script might try to modify shell rc files.
  # We need to ensure NVM environment is sourced for *this script's* execution.
  # Since this script is run as root, NVM will be installed for the root user.
  export NVM_DIR="$HOME/.nvm" # Standard NVM directory for the current user (root in this case)
  
  # Download and execute NVM install script
  # Using a specific stable version of NVM installer for consistency
  if curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash; then
    info "NVM installed or updated."
  else
    error "Failed to download or run NVM install script."
    return 1 # Use return for functions, exit for script termination
  fi

  # Source NVM script to make nvm command available
  # This ensures NVM is available for the current script execution.
  # The NVM install script usually adds this to .bashrc, but we need it now.
  [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"
  [ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"

  # Check if nvm command is available
  if ! command -v nvm &> /dev/null; then
    error "NVM command not found after installation. Sourcing NVM might have failed."
    error "Please try sourcing NVM manually in your shell and re-running parts of the script if needed:"
    error "  export NVM_DIR=\"\$([ -z \"\$XDG_CONFIG_HOME\" ] && printf %s \"\$HOME/.nvm\" || printf %s \"\$XDG_CONFIG_HOME/nvm\")\""
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
  
  nvm alias default "$NODE_VERSION" # Set default node version for the NVM instance
  nvm use default # Use the installed version for the current session

  # Get the absolute path to the installed Node executable
  # This is crucial for systemd service files which don't inherit shell environments easily.
  NODE_EXEC_PATH=$(nvm_find_node_path "$NODE_VERSION") # nvm_find_node_path is an internal nvm function
  if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
      # Fallback if nvm_find_node_path isn't available or fails
      NODE_EXEC_PATH=$(command -v node) # Tries to get from current PATH after nvm use
      if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
          # More robust find if 'which node' fails in some contexts
          NODE_EXEC_PATH=$(find "$NVM_DIR/versions/node" -path "*/v$NODE_VERSION.*/bin/node" -type f 2>/dev/null | head -n 1)
      fi
  fi

  if [ -z "$NODE_EXEC_PATH" ] || [ ! -f "$NODE_EXEC_PATH" ]; then
    error "Could not determine the absolute path to the Node.js v$NODE_VERSION executable."
    error "NVM_DIR is $NVM_DIR. Searched for node under versions/node/v$NODE_VERSION.*/bin/node"
    return 1
  fi
  info "Node.js executable path set to: $NODE_EXEC_PATH"
  
  # Get path for NPM
  NPM_EXEC_PATH="$(dirname "$NODE_EXEC_PATH")/npm"
  if [ ! -f "$NPM_EXEC_PATH" ]; then
      error "Could not determine the absolute path to npm. Expected at $(dirname "$NODE_EXEC_PATH")/npm"
      NPM_EXEC_PATH="" # Ensure it's empty if not found
      return 1
  fi
  info "NPM executable path set to: $NPM_EXEC_PATH"
  
  # Verify Node and npm versions
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
    # Stash any local changes to avoid pull conflicts, then pull
    # Only stash if there are changes to stash
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
          # Attempt to pop the stash if one was created by this script and pull failed
          if git stash list | grep -q "cakehole-setup-script-stash"; then
              git stash pop || info "No stash to pop or stash pop failed after failed pull."
          fi
          exit 1 # Exit if pull fails
      fi
    fi
    # Attempt to pop the stash if one was created by this script
    if git stash list | grep -q "cakehole-setup-script-stash"; then
        git stash pop || info "Stash pop failed after successful pull, resolve manually if needed."
    fi

    # Ensure we cd back to the original directory if the script was not started from $INSTALL_DIR
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
  # NODE_EXEC_PATH is now a global variable set by install_node

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
# Environment=NODE_ENV=production # Optional: for production Node.js apps

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
  install_base_dependencies # Ensures git and curl are present

  if ! install_node; then # This will install NVM, Node, NPM and set NODE_EXEC_PATH, NPM_EXEC_PATH
    error "Node.js installation failed. Aborting."
    exit 1
  fi
  
  # Verify paths were set by install_node
  if [ -z "$NODE_EXEC_PATH" ] || [ -z "$NPM_EXEC_PATH" ]; then
      error "NODE_EXEC_PATH or NPM_EXEC_PATH was not set by install_node. Aborting."
      exit 1
  fi

  setup_repository

  local final_server_binary="$INSTALL_DIR/$SERVER_BINARY_REL_PATH"
  local final_web_server_dir="$INSTALL_DIR/$WEB_SERVER_DIR_REL_PATH"

  # Change to the installation directory for context if any sub-scripts from the repo need it.
  if [ "$PWD" != "$INSTALL_DIR" ]; then
    cd "$INSTALL_DIR" || { error "Failed to change directory to $INSTALL_DIR. Aborting."; exit 1; }
  fi
  
  # Install npm dependencies for the web server if package.json exists
  if [ -f "$final_web_server_dir/package.json" ]; then
    info "Found package.json in $final_web_server_dir. Running npm install..."
    if [ ! -d "$final_web_server_dir" ]; then
        error "Web server directory $final_web_server_dir does not exist. Cannot run npm install."
    else
        pushd "$final_web_server_dir" > /dev/null || { error "Failed to cd to $final_web_server_dir"; exit 1; }
        # Use the globally set NPM_EXEC_PATH
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

  if ! create_dns_service "$final_server_binary"; then
    error "DNS service creation failed. Please check logs."
  fi

  # Pass NODE_EXEC_PATH to create_web_service, though it's global, explicit is fine too
  if ! create_web_service "$final_web_server_dir"; then 
    error "Web service creation failed. Please check logs."
  fi

  info "--------------------------------------------------------------------"
  info "CakeHole ($PROJECT_NAME) setup and service installation complete!"
  info "--------------------------------------------------------------------"
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
