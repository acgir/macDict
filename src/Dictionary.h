#ifndef INCLUDED_DICTIONARY_H
#define INCLUDED_DICTIONARY_H

struct DictionaryRef;

void output_body_css(const bool dark, std::ostream &out);

/// Trim whitespace at the left and right
void strip(std::string &s);

int output_definition(
	const DictionaryRef &d,
	const std::string &target,
	const bool embed_default_css,
	const bool dark,
	std::ostream &out,
	std::ostream &err);

void list_words(
	const DictionaryRef &d,
	const std::string &target,
	void (*func)(const std::string &, void *data),
	void *data);

#endif
