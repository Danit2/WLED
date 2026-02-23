Import("env")

# Reference to the current build environment
projenv = env

# Read the custom_usermods option from platformio.ini (WLED 0.16 structure)
custom_usermods = projenv.GetProjectOption("custom_usermods", default="")

# Convert the string into a clean uppercase list
# Supports comma or space separated entries
usermod_list = [
    u.strip().upper()
    for u in custom_usermods.replace(",", " ").split()
]

# Check if FSEQ or wildcard "*" is selected
fseq_enabled = (
    "FSEQ" in usermod_list or
    "*" in usermod_list
)

# Get current CPPDEFINES (build flags)
cpp_defines = projenv.get("CPPDEFINES", [])

# Extract define names into a simple list
define_names = []
for d in cpp_defines:
    if isinstance(d, tuple):
        define_names.append(d[0])
    else:
        define_names.append(d)

# Check if MMC or SPI is already enabled
mmc_enabled = "WLED_USE_SD_MMC" in define_names
spi_enabled = "WLED_USE_SD_SPI" in define_names

# Logic:
# If FSEQ usermod is selected
# AND neither MMC nor SPI is already defined
# then automatically enable SPI
if fseq_enabled and not mmc_enabled and not spi_enabled:
    print("FSEQ usermod detected -> enabling WLED_USE_SD_SPI")
    projenv.Append(CPPDEFINES=["WLED_USE_SD_SPI"])