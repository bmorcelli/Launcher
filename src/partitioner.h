#ifndef __PARTITIONER_H
#define __PARTITIONER_H

#include <Arduino.h>

void partList();

void dumpPartition(const char *partitionLabel, const char *outputPath);

void restorePartition(const char *partitionLabel);

bool attachPartition(String from, String to);

void partitionCrawler();

#endif /*__PARTITIONER_H*/
