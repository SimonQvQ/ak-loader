#pragma once

// Resolve an exported host function from one exact, already mapped ELF file.
// Does not load a new DSO or inspect private Android namespace structures.
void *find_loaded_export(const char *absolute_path, const char *symbol,
                         const char **reason = nullptr);
