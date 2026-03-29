#pragma once

#include <string>
#include <vector>

#include "EpubAnnotationRecord.h"

/** Lightweight reader for one saved annotation page. */
namespace EpubAnnotationStorage {

bool load(const std::string& cachePath, int spine, int page, std::vector<EpubAnnotationRecord>& records);
bool remove(const std::string& cachePath, const EpubAnnotationRecord& record);
bool update(const std::string& cachePath, const EpubAnnotationRecord& oldRecord,
            const EpubAnnotationRecord& updatedRecord);

}  // namespace EpubAnnotationStorage
