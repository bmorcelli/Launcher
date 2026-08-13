import os
import json
from SCons.Script import Import

Import("env")

# Identifica a board selecionada no platformio.ini
board = env["BOARD"]

# Caminho para o JSON correspondente à board selecionada
BOARD_JSON_PATH = os.path.join(env["PROJECT_DIR"], "boards","_jsonfiles", f"{board}.json")

def load_board_config():
    """Carrega as configurações da board a partir do JSON."""
    try:
        with open(BOARD_JSON_PATH, "r") as file:
            return json.load(file)
    except Exception as e:
        print(f"Erro ao carregar {BOARD_JSON_PATH}: {e}")
        return {}

def generate_build_flags(board_config):
    """Gera as build_flags dinamicamente baseado no JSON da board."""
    flags = []

    # Configurações de hardware
    flags.append("-DHAS_TOUCH=1")

    # Verifica os drivers de video habilitados na board
    # TFT_DATABUS_N / TFT_DISPLAY_DRIVER_N are DisplayDrivers' way of naming the
    # data bus and the panel controller. See the library README for the tables.
    extra_flags = board_config.get("build", {}).get("extra_flags", [])
    if any("DISPLAY_ILI9341_SPI" in flag for flag in extra_flags):
        flags.append("-DTFT_DATABUS_N=0")        # Arduino_HWSPI
        flags.append("-DTFT_DISPLAY_DRIVER_N=4") # Arduino_ILI9341
        flags.append("-DTFT_MISO=ILI9341_SPI_BUS_MISO_IO_NUM")
        flags.append("-DTFT_MOSI=ILI9341_SPI_BUS_MOSI_IO_NUM")
        flags.append("-DTFT_SCLK=ILI9341_SPI_BUS_SCLK_IO_NUM")
        flags.append("-DTFT_CS=ILI9341_SPI_CONFIG_CS_GPIO_NUM")
        flags.append("-DTFT_DC=ILI9341_SPI_CONFIG_DC_GPIO_NUM")
        flags.append("-DTFT_RST=ILI9341_DEV_CONFIG_RESET_GPIO_NUM")
        flags.append("-DTFT_BL=GPIO_BCKL")
        flags.append("-DTFT_WIDTH=DISPLAY_WIDTH")
        flags.append("-DTFT_HEIGHT=DISPLAY_HEIGHT")
        flags.append("-DTFT_IPS=0")
        flags.append("-DTFT_COL_OFS1=0")
        flags.append("-DTFT_ROW_OFS1=0")
        flags.append("-DTFT_COL_OFS2=0")
        flags.append("-DTFT_ROW_OFS2=0")
        flags.append("-DROTATION=0")

    elif any("DISPLAY_ST7796_SPI" in flag for flag in extra_flags):
        flags.append("-DTFT_DATABUS_N=0")        # Arduino_HWSPI
        flags.append("-DTFT_DISPLAY_DRIVER_N=2") # Arduino_ST7796
        flags.append("-DTFT_MISO=ST7796_SPI_BUS_MISO_IO_NUM")
        flags.append("-DTFT_MOSI=ST7796_SPI_BUS_MOSI_IO_NUM")
        flags.append("-DTFT_SCLK=ST7796_SPI_BUS_SCLK_IO_NUM")
        flags.append("-DTFT_CS=ST7796_SPI_CONFIG_CS_GPIO_NUM")
        flags.append("-DTFT_DC=ST7796_SPI_CONFIG_DC_GPIO_NUM")
        flags.append("-DTFT_RST=ST7796_DEV_CONFIG_RESET_GPIO_NUM")
        flags.append("-DTFT_BL=GPIO_BCKL")
        flags.append("-DTFT_WIDTH=DISPLAY_WIDTH")
        flags.append("-DTFT_HEIGHT=DISPLAY_HEIGHT")
        flags.append("-DTFT_IPS=0")
        flags.append("-DTFT_COL_OFS1=0")
        flags.append("-DTFT_ROW_OFS1=0")
        flags.append("-DTFT_COL_OFS2=0")
        flags.append("-DTFT_ROW_OFS2=0")
        flags.append("-DROTATION=0")

    elif any("DISPLAY_ST7789_SPI" in flag for flag in extra_flags):
        flags.append("-DTFT_DATABUS_N=0")        # Arduino_HWSPI
        flags.append("-DTFT_DISPLAY_DRIVER_N=1") # Arduino_ST7789
        flags.append("-DTFT_MISO=ST7789_SPI_BUS_MISO_IO_NUM")
        flags.append("-DTFT_MOSI=ST7789_SPI_BUS_MOSI_IO_NUM")
        flags.append("-DTFT_SCLK=ST7789_SPI_BUS_SCLK_IO_NUM")
        flags.append("-DTFT_CS=ST7789_SPI_CONFIG_CS_GPIO_NUM")
        flags.append("-DTFT_DC=ST7789_SPI_CONFIG_DC_GPIO_NUM")
        flags.append("-DTFT_RST=ST7789_DEV_CONFIG_RESET_GPIO_NUM")
        flags.append("-DTFT_BL=GPIO_BCKL")
        flags.append("-DTFT_WIDTH=DISPLAY_WIDTH")
        flags.append("-DTFT_HEIGHT=DISPLAY_HEIGHT")
        flags.append("-DTFT_IPS=0")
        flags.append("-DTFT_COL_OFS1=0")
        flags.append("-DTFT_ROW_OFS1=0")
        flags.append("-DTFT_COL_OFS2=0")
        flags.append("-DTFT_ROW_OFS2=0")
        flags.append("-DROTATION=0")

    elif any("DISPLAY_AXS15231B_QSPI" in flag for flag in extra_flags):
        flags.append("-DTFT_DATABUS_N=1")         # Arduino_ESP32QSPI
        flags.append("-DTFT_DISPLAY_DRIVER_N=22") # Arduino_AXS15231B
        flags.append("-DTFT_MISO=-1")
        flags.append("-DTFT_MOSI=-1")
        flags.append("-DTFT_D0=AXS15231B_SPI_BUS_DATA0")
        flags.append("-DTFT_D1=AXS15231B_SPI_BUS_DATA1")
        flags.append("-DTFT_D2=AXS15231B_SPI_BUS_DATA2")
        flags.append("-DTFT_D3=AXS15231B_SPI_BUS_DATA3")
        flags.append("-DTFT_SCLK=AXS15231B_SPI_BUS_PCLK")
        flags.append("-DTFT_CS=AXS15231B_SPI_CONFIG_CS")
        flags.append("-DTFT_DC=AXS15231B_SPI_CONFIG_DC")
        flags.append("-DTFT_RST=AXS15231B_DEV_CONFIG_RESET")
        flags.append("-DTFT_BL=GPIO_BCKL")
        flags.append("-DTFT_WIDTH=DISPLAY_WIDTH")
        flags.append("-DTFT_HEIGHT=DISPLAY_HEIGHT")
        flags.append("-DTFT_IPS=0")
        flags.append("-DTFT_COL_OFS1=0")
        flags.append("-DTFT_ROW_OFS1=0")
        flags.append("-DTFT_COL_OFS2=0")
        flags.append("-DTFT_ROW_OFS2=0")
        flags.append("-DROTATION=0")


    elif any("DISPLAY_ST7789_I80" in flag for flag in extra_flags):
        # D0-D7 are spread over both GPIO banks here, so it is the generic
        # 8-bit writer (bus 5) and not the faster same-bank PAR8Q (bus 2).
        flags.append("-DTFT_DATABUS_N=5")        # Arduino_ESP32PAR8
        flags.append("-DTFT_DISPLAY_DRIVER_N=1") # Arduino_ST7789
        flags.append("-DTFT_INVERSION_OFF")
        flags.append("-DTFT_WIDTH=DISPLAY_WIDTH")
        flags.append("-DTFT_HEIGHT=DISPLAY_HEIGHT")
        flags.append("-DTFT_CS=ST7789_IO_I80_CONFIG_CS_GPIO_NUM")
        flags.append("-DTFT_DC=ST7789_I80_BUS_CONFIG_DC")
        flags.append("-DTFT_RST=ST7789_DEV_CONFIG_RESET_GPIO_NUM")
        flags.append("-DTFT_WR=ST7789_I80_BUS_CONFIG_WR")
        flags.append("-DTFT_RD=ST7789_RD_GPIO")
        flags.append("-DTFT_D0=ST7789_I80_BUS_CONFIG_DATA_GPIO_D8")
        flags.append("-DTFT_D1=ST7789_I80_BUS_CONFIG_DATA_GPIO_D9")
        flags.append("-DTFT_D2=ST7789_I80_BUS_CONFIG_DATA_GPIO_D10")
        flags.append("-DTFT_D3=ST7789_I80_BUS_CONFIG_DATA_GPIO_D11")
        flags.append("-DTFT_D4=ST7789_I80_BUS_CONFIG_DATA_GPIO_D12")
        flags.append("-DTFT_D5=ST7789_I80_BUS_CONFIG_DATA_GPIO_D13")
        flags.append("-DTFT_D6=ST7789_I80_BUS_CONFIG_DATA_GPIO_D14")
        flags.append("-DTFT_D7=ST7789_I80_BUS_CONFIG_DATA_GPIO_D15")
        flags.append("-DTFT_BCKL=GPIO_BCKL")
        flags.append("-DTFT_BL=GPIO_BCKL")
        flags.append("-DTFT_BUS_SHARED=0")
        flags.append("-DTFT_INVERTED=0")
        flags.append("-DTFT_IPS=0")
        flags.append("-DTFT_COL_OFS1=0")
        flags.append("-DTFT_ROW_OFS1=0")
        flags.append("-DTFT_COL_OFS2=0")
        flags.append("-DTFT_ROW_OFS2=0")
        flags.append("-DROTATION=0")

    elif any("DISPLAY_ST7262_PAR" in flag for flag in extra_flags):
        flags.append("-DTFT_DATABUS_N=3")         # Arduino_ESP32RGBPanel
        flags.append("-DTFT_DISPLAY_DRIVER_N=49") # Arduino_RGB_Display
        flags.append("-DTFT_DE=ST7262_PANEL_CONFIG_DE_GPIO_NUM")
        flags.append("-DTFT_VSYNC=ST7262_PANEL_CONFIG_VSYNC_GPIO_NUM")
        flags.append("-DTFT_HSYNC=ST7262_PANEL_CONFIG_HSYNC_GPIO_NUM")
        flags.append("-DTFT_PCLK=ST7262_PANEL_CONFIG_PCLK_GPIO_NUM")
        # This panel wires R and B the other way round.
        flags.append("-DTFT_R0=ST7262_PANEL_CONFIG_DATA_GPIO_B0")
        flags.append("-DTFT_R1=ST7262_PANEL_CONFIG_DATA_GPIO_B1")
        flags.append("-DTFT_R2=ST7262_PANEL_CONFIG_DATA_GPIO_B2")
        flags.append("-DTFT_R3=ST7262_PANEL_CONFIG_DATA_GPIO_B3")
        flags.append("-DTFT_R4=ST7262_PANEL_CONFIG_DATA_GPIO_B4")
        flags.append("-DTFT_G0=ST7262_PANEL_CONFIG_DATA_GPIO_G0")
        flags.append("-DTFT_G1=ST7262_PANEL_CONFIG_DATA_GPIO_G1")
        flags.append("-DTFT_G2=ST7262_PANEL_CONFIG_DATA_GPIO_G2")
        flags.append("-DTFT_G3=ST7262_PANEL_CONFIG_DATA_GPIO_G3")
        flags.append("-DTFT_G4=ST7262_PANEL_CONFIG_DATA_GPIO_G4")
        flags.append("-DTFT_G5=ST7262_PANEL_CONFIG_DATA_GPIO_G5")
        flags.append("-DTFT_B0=ST7262_PANEL_CONFIG_DATA_GPIO_R0")
        flags.append("-DTFT_B1=ST7262_PANEL_CONFIG_DATA_GPIO_R1")
        flags.append("-DTFT_B2=ST7262_PANEL_CONFIG_DATA_GPIO_R2")
        flags.append("-DTFT_B3=ST7262_PANEL_CONFIG_DATA_GPIO_R3")
        flags.append("-DTFT_B4=ST7262_PANEL_CONFIG_DATA_GPIO_R4")
        flags.append("-DTFT_HSYNC_POL=0")
        flags.append("-DTFT_VSYNC_POL=0")
        flags.append("-DTFT_HSYNC_FRONT_PORCH=ST7262_PANEL_CONFIG_TIMINGS_HSYNC_FRONT_PORCH")
        flags.append("-DTFT_HSYNC_PULSE_WIDTH=ST7262_PANEL_CONFIG_TIMINGS_HSYNC_PULSE_WIDTH")
        flags.append("-DTFT_HSYNC_BACK_PORCH=ST7262_PANEL_CONFIG_TIMINGS_HSYNC_BACK_PORCH")
        flags.append("-DTFT_VSYNC_FRONT_PORCH=ST7262_PANEL_CONFIG_TIMINGS_VSYNC_FRONT_PORCH")
        flags.append("-DTFT_VSYNC_PULSE_WIDTH=ST7262_PANEL_CONFIG_TIMINGS_VSYNC_PULSE_WIDTH")
        flags.append("-DTFT_VSYNC_BACK_PORCH=ST7262_PANEL_CONFIG_TIMINGS_VSYNC_BACK_PORCH")
        flags.append("-DTFT_PCLK_ACTIVE_NEG=ST7262_PANEL_CONFIG_TIMINGS_FLAGS_PCLK_ACTIVE_NEG")
        flags.append("-DTFT_PREF_SPEED=16000000")
        flags.append("-DTFT_BL=GPIO_BCKL")
        flags.append("-DTFT_WIDTH=DISPLAY_WIDTH")
        flags.append("-DTFT_HEIGHT=DISPLAY_HEIGHT")
        flags.append("-DROTATION=0")

    elif any("DISPLAY_ST7701_PAR" in flag for flag in extra_flags):
        flags.append("-DTFT_DATABUS_N=3")         # Arduino_ESP32RGBPanel
        flags.append("-DTFT_DISPLAY_DRIVER_N=49") # Arduino_RGB_Display
        flags.append("-DTFT_DE=ST7701_PANEL_CONFIG_DE_GPIO_NUM")
        flags.append("-DTFT_VSYNC=ST7701_PANEL_CONFIG_VSYNC_GPIO_NUM")
        flags.append("-DTFT_HSYNC=ST7701_PANEL_CONFIG_HSYNC_GPIO_NUM")
        flags.append("-DTFT_PCLK=ST7701_PANEL_CONFIG_PCLK_GPIO_NUM")
        flags.append("-DTFT_R0=ST7701_PANEL_CONFIG_DATA_GPIO_R0")
        flags.append("-DTFT_R1=ST7701_PANEL_CONFIG_DATA_GPIO_R1")
        flags.append("-DTFT_R2=ST7701_PANEL_CONFIG_DATA_GPIO_R2")
        flags.append("-DTFT_R3=ST7701_PANEL_CONFIG_DATA_GPIO_R3")
        flags.append("-DTFT_R4=ST7701_PANEL_CONFIG_DATA_GPIO_R4")
        flags.append("-DTFT_G0=ST7701_PANEL_CONFIG_DATA_GPIO_G0")
        flags.append("-DTFT_G1=ST7701_PANEL_CONFIG_DATA_GPIO_G1")
        flags.append("-DTFT_G2=ST7701_PANEL_CONFIG_DATA_GPIO_G2")
        flags.append("-DTFT_G3=ST7701_PANEL_CONFIG_DATA_GPIO_G3")
        flags.append("-DTFT_G4=ST7701_PANEL_CONFIG_DATA_GPIO_G4")
        flags.append("-DTFT_G5=ST7701_PANEL_CONFIG_DATA_GPIO_G5")
        flags.append("-DTFT_B0=ST7701_PANEL_CONFIG_DATA_GPIO_B0")
        flags.append("-DTFT_B1=ST7701_PANEL_CONFIG_DATA_GPIO_B1")
        flags.append("-DTFT_B2=ST7701_PANEL_CONFIG_DATA_GPIO_B2")
        flags.append("-DTFT_B3=ST7701_PANEL_CONFIG_DATA_GPIO_B3")
        flags.append("-DTFT_B4=ST7701_PANEL_CONFIG_DATA_GPIO_B4")
        flags.append("-DTFT_HSYNC_POL=1")
        flags.append("-DTFT_VSYNC_POL=1")
        flags.append("-DTFT_HSYNC_FRONT_PORCH=ST7701_PANEL_CONFIG_TIMINGS_HSYNC_FRONT_PORCH")
        flags.append("-DTFT_HSYNC_PULSE_WIDTH=ST7701_PANEL_CONFIG_TIMINGS_HSYNC_PULSE_WIDTH")
        flags.append("-DTFT_HSYNC_BACK_PORCH=ST7701_PANEL_CONFIG_TIMINGS_HSYNC_BACK_PORCH")
        flags.append("-DTFT_VSYNC_FRONT_PORCH=ST7701_PANEL_CONFIG_TIMINGS_VSYNC_FRONT_PORCH")
        flags.append("-DTFT_VSYNC_PULSE_WIDTH=ST7701_PANEL_CONFIG_TIMINGS_VSYNC_PULSE_WIDTH")
        flags.append("-DTFT_VSYNC_BACK_PORCH=ST7701_PANEL_CONFIG_TIMINGS_VSYNC_BACK_PORCH")
        flags.append("-DTFT_PCLK_ACTIVE_NEG=ST7701_PANEL_CONFIG_TIMINGS_FLAGS_PCLK_ACTIVE_NEG")
        flags.append("-DTFT_PREF_SPEED=GFX_NOT_DEFINED")
        flags.append("-DTFT_BL=GPIO_BCKL")
        flags.append("-DTFT_WIDTH=DISPLAY_WIDTH")
        flags.append("-DTFT_HEIGHT=DISPLAY_HEIGHT")
        flags.append("-DROTATION=0")


    else:
        flags.append("-DTFT_DATABUS_N=0")        # Arduino_HWSPI
        flags.append("-DTFT_DISPLAY_DRIVER_N=4") # Arduino_ILI9341
        flags.append("-DTFT_MISO=12")
        flags.append("-DTFT_MOSI=13")
        flags.append("-DTFT_SCLK=14")
        flags.append("-DTFT_CS=15")
        flags.append("-DTFT_DC=2")
        flags.append("-DTFT_RST=-1")
        flags.append("-DTFT_BL=21")
        flags.append("-DTFT_WIDTH=240")
        flags.append("-DTFT_HEIGHT=320")
        flags.append("-DTFT_IPS=0")
        flags.append("-DTFT_COL_OFS1=0")
        flags.append("-DTFT_ROW_OFS1=0")
        flags.append("-DTFT_COL_OFS2=0")
        flags.append("-DTFT_ROW_OFS2=0")
        flags.append("-DROTATION=0")

    # Verifica suporte ao touch
    if any("TOUCH_XPT2046_SPI" in flag for flag in extra_flags):
        flags.append("-DHAS_RESISTIVE_TOUCH=1")
        flags.append("-DCYD28_TouchR_IRQ=XPT2046_TOUCH_CONFIG_INT_GPIO_NUM")
        flags.append("-DCYD28_TouchR_MISO=XPT2046_SPI_BUS_MISO_IO_NUM")
        flags.append("-DCYD28_TouchR_MOSI=XPT2046_SPI_BUS_MOSI_IO_NUM")
        flags.append("-DCYD28_TouchR_CLK=XPT2046_SPI_BUS_SCLK_IO_NUM")
        flags.append("-DCYD28_TouchR_CS=XPT2046_SPI_CONFIG_CS_GPIO_NUM")

    # Verifica suporte a cartão SD
    if any("BOARD_HAS_TF" in flag for flag in extra_flags):
        flags.append("-DSDCARD_CS=TF_CS")
        flags.append("-DSDCARD_SCK=TF_SPI_SCLK")
        flags.append("-DSDCARD_MISO=TF_SPI_MISO")
        flags.append("-DSDCARD_MOSI=TF_SPI_MOSI")
    else:
        flags.append("-DSDCARD_CS=5")
        flags.append("-DSDCARD_SCK=18")
        flags.append("-DSDCARD_MISO=19")
        flags.append("-DSDCARD_MOSI=23")

    return flags

# Carregar configurações do JSON da board correspondente
board_config = load_board_config()

# Gerar as build_flags dinamicamente
build_flags = generate_build_flags(board_config)

# Adicionar as build_flags ao ambiente do PlatformIO
print("Adicionando build_flags dinâmicas:", build_flags)
env.Append(CPPDEFINES=build_flags)
