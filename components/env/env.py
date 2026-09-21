import os
import re

Import("env")

# Path to your .env file
env_file_path = os.path.join(env.get("PROJECT_DIR"), ".env")

if os.path.exists(env_file_path):
    print(f"--- [ENV] Loading variables from {env_file_path} ---")
    with open(env_file_path, "r") as f:
        for line in f:
            # Skip comments and empty lines
            line = line.strip()
            if not line or line.startswith("#"):
                continue
                
            # Parse KEY=VALUE
            match = re.match(r"^([^=]+)=(.*)$", line)
            if match:
                key = match.group(1).strip()
                val = match.group(2).strip()
                
                # Strip wrapping quotes if they exist in the file
                if (val.startswith('"') and val.endswith('"')) or (val.startswith("'") and val.endswith("'")):
                    val = val[1:-1]
                
                # Escape quotes around strings so they pass safely into C/C++ macros
                escaped_val = f'\\"{val}\\"'
                
                # Append to PlatformIO build flags
                env.Append(BUILD_FLAGS=[f"-D{key}={escaped_val}"])
                print(f"    Added macro: -D{key}={escaped_val}")
else:
    print(f"--- [ENV] Warning: .env file not found at {env_file_path} ---")
