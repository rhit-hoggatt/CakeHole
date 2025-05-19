#!/bin/bash

# CakeHole Service Setup Script
# Assumes git, curl, Node.js, and npm are already installed and in the PATH.
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

# Check for essential commands and set their paths
check_commands() {
    info "Checking for required commands..."
    NODE_EXEC_PATH=$(command -v node)
    NPM_EXEC_PATH=$(command -v npm)
    local git_path
    git_path=$(command -v git)

    if [ -z "$NODE_EXEC_PATH" ]; then
        error "Node.js (node) command not found in PATH. Please install Node.js."
        exit 1
    fi
    if [ -z "$NPM_EXEC_PATH" ]; then
        error "npm command not found in PATH. Please install npm (usually comes with Node.js)."
        exit 1
    fi
    if [ -z "$git_path" ]; then
        error "git command not found in PATH. Please install git."
        exit 1
    fi
    info "Required commands (node, npm, git) found."
    info "Node path: $NODE_EXEC_PATH"
    info "NPM path: $NPM_EXEC_PATH"
}


# Clone or update the repository
setup_repository() {
  info "Setting up repository in $INSTALL_DIR..."
  if [ -d "$INSTALL_DIR/.git" ]; then
    info "Existing installation found in $INSTALL_DIR. Attempting to update..."
    cd "$INSTALL_DIR" || { error "Failed to cd into $INSTALL_DIR"; exit 1; }
    # Stash any local changes to avoid pull conflicts, then pull
    if git stash push -u -m "cakehole-setup-script-stash-$(date +%s)"; then
        info "Stashed local changes if any."
    fi
    if git pull --rebase; then # Using rebase to avoid merge commits for a cleaner history
      info "Repository updated successfully."
    else
      error "Failed to update repository. Trying git pull without rebase..."
      if git pull; then
          info "Repository updated successfully (with merge)."
      else
          error "Failed to update repository even with a standard pull. Please check $INSTALL_DIR for conflicts."
          # Optionally, try to pop the stash if pull failed
          git stash pop || info "No stash to pop or stash pop failed."
          exit 1
      fi
    fi
    # Attempt to pop the stash if one was created by this script
    if git stash list | grep -q "cakehole-setup-script-stash"; then
        git stash pop || info "Stash pop failed, resolve manually if needed."
    fi

    # Ensure we cd back to the original directory if the script was not started from $INSTALL_DIR
    # Check if OLDPWD is set and not empty before trying to cd to it.
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
  if systemctl restart "$SERVICE_NAME_DNS"; then # Use restart to ensure changes are applied
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
  local node_exec="$2" # Expects absolute path to node

  if [ ! -d "$web_server_abs_dir" ]; then
    error "Web server directory not found at $web_server_abs_dir"
    return 1
  fi
  if [ ! -f "$web_server_abs_dir/server.js" ]; then 
    error "Web server main file 'server.js' not found in $web_server_abs_dir"
    return 1
  fi
  if [ -z "$node_exec" ] || [ ! -x "$node_exec" ]; then # Check if node_exec is executable
    error "Node.js executable path '$node_exec' is invalid or not executable."
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
ExecStart=$node_exec server.js
Restart=on-failure
RestartSec=5
# Environment=NODE_ENV=production # Optional: for production Node.js apps
# Environment="PATH=/usr/bin:/usr/local/bin:$PATH" # May help systemd find node if not absolute

[Install]
WantedBy=multi-user.target
EOF

  systemctl daemon-reload
  systemctl enable "$SERVICE_NAME_WEB"
  if systemctl restart "$SERVICE_NAME_WEB"; then # Use restart to ensure changes are applied
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
  check_commands # This will set NODE_EXEC_PATH and NPM_EXEC_PATH

  setup_repository

  local final_server_binary="$INSTALL_DIR/$SERVER_BINARY_REL_PATH"
  local final_web_server_dir="$INSTALL_DIR/$WEB_SERVER_DIR_REL_PATH"

  # Change to the installation directory for context if any sub-scripts from the repo need it.
  # However, for service files, absolute paths are used.
  if [ "$PWD" != "$INSTALL_DIR" ]; then
    cd "$INSTALL_DIR" || { error "Failed to change directory to $INSTALL_DIR. Aborting."; exit 1; }
  fi
  
  # Install npm dependencies for the web server if package.json exists
  if [ -f "$final_web_server_dir/package.json" ]; then
    info "Found package.json in $final_web_server_dir. Running npm install..."
    if [ ! -d "$final_web_server_dir" ]; then
        error "Web server directory $final_web_server_dir does not exist. Cannot run npm install."
        # This should not happen if setup_repository worked and WEB_SERVER_DIR_REL_PATH is correct
    else
        # Temporarily change to the web server directory to run npm install
        pushd "$final_web_server_dir" > /dev/null || { error "Failed to cd to $final_web_server_dir"; exit 1; }
        if "$NPM_EXEC_PATH" install --omit=dev; then # --omit=dev is good for production
          info "npm install completed successfully in $final_web_server_dir."
        else
          error "npm install failed in $final_web_server_dir. Check for errors above."
          # The script will continue, but the web service might not work.
        fi
        popd > /dev/null || { error "Failed to popd from $final_web_server_dir"; exit 1; }
    fi
  else
    info "No package.json found in $final_web_server_dir, skipping npm install."
  fi

  if ! create_dns_service "$final_server_binary"; then
    error "DNS service creation failed. Please check logs."
    # Decide if you want to exit or continue
  fi

  if ! create_web_service "$final_web_server_dir" "$NODE_EXEC_PATH"; then
    error "Web service creation failed. Please check logs."
  fi

  info "--------------------------------------------------------------------"
  info "CakeHole ($PROJECT_NAME) service setup attempt complete!"
  info "--------------------------------------------------------------------"
  info "Services should now be enabled and running."
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
