import os

Import("env")

framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if not framework:
	raise RuntimeError("Arduino framework is missing; cannot patch the SD driver")
path = os.path.join(framework, "libraries", "SD", "src", "sd_diskio.cpp")
with open(path, encoding="utf-8") as source:
	content = source.read()

marker = "LAUNCHER_EXPANDER_SD_PATCH"
if marker not in content:
	def replace_once(old, new):
		global content
		if content.count(old) != 1:
			raise RuntimeError("SD driver changed; cannot apply expander patch")
		content = content.replace(old, new, 1)

	replace_once('#include "esp32-hal-periman.h"', '''#include "esp32-hal-periman.h"

// LAUNCHER_EXPANDER_SD_PATCH
#ifdef LAUNCHER_EXPANDER_SD
bool launcherSdBusBegin();
void launcherSdBusEnd();
bool launcherSdChipSelect(bool high);
#define SD_SELECT(pin, high) launcherSdChipSelect(high)
#else
#define SD_SELECT(pin, high) (digitalWrite(pin, high), true)
#endif''')
	replace_once('  digitalWrite(card->ssPin, LOW);\n  bool s = sdWait',
		'  if (!SD_SELECT(card->ssPin, LOW)) return false;\n  bool s = sdWait')
	replace_once('  digitalWrite(card->ssPin, LOW);\n  if (!sdWait(pdrv, 500))',
		'  if (!SD_SELECT(card->ssPin, LOW)) goto unknown_card;\n  if (!sdWait(pdrv, 500))')
	if content.count('digitalWrite(card->ssPin, HIGH);') != 4:
		raise RuntimeError("SD driver changed; cannot patch chip select")
	content = content.replace('digitalWrite(card->ssPin, HIGH);', 'SD_SELECT(card->ssPin, HIGH);')
	replace_once('    card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));',
		'''    card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));
#ifdef LAUNCHER_EXPANDER_SD
    launcherSdBusBegin();
#endif''')
	replace_once('    card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));',
		'''    card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
#ifdef LAUNCHER_EXPANDER_SD
    launcherSdBusBegin();
#endif''')
	replace_once('    card->spi->endTransaction();', '''#ifdef LAUNCHER_EXPANDER_SD
    launcherSdBusEnd();
#endif
    card->spi->endTransaction();''')
	replace_once('  pinMode(card->ssPin, OUTPUT);\n  SD_SELECT(card->ssPin, HIGH);\n  perimanSetPinBusExtraType(card->ssPin, "SD_SS");',
		'''#ifndef LAUNCHER_EXPANDER_SD
  pinMode(card->ssPin, OUTPUT);
  SD_SELECT(card->ssPin, HIGH);
  perimanSetPinBusExtraType(card->ssPin, "SD_SS");
#endif''')
	with open(path, "w", encoding="utf-8") as source:
		source.write(content)
