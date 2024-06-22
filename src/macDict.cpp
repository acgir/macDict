// Copyright (C) 2023 craig

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <zlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <map>
#include <unordered_map>
#include <vector>
#include <set>
#include <cctype>
#include <cstring>
#include <cassert>

#ifdef WANT_GUI
#include <QtWidgets/QApplication>
#include <QScreen>
#include "Window.h"
#endif

using std::cout;
using std::cerr;

static const char g_xpath_derivatives[] =
	"//span[contains(@class, \"t_derivatives\")]//"
	"span[contains(@class, \"x_xoh\")]/"
	"span[@role=\"text\" and not (@class=\"gg\" or @class=\"posg\")]/text()";

static const char g_xpath_phrases[] =
	"//span[contains(@class, \"t_phrases\")]//"
	"span[@role=\"text\" and contains(@class, \"l\")]/text()";

// dog and bone
static const char g_xpath_phrases_other[] =
	"//span[contains(@class, \"t_phrases\")]//"
	"span[@class=\"vg\"]/span[@class=\"v\"]/text()";

// bang on
static const char g_xpath_phrasal_verbs[] =
	"//span[contains(@class, \"t_phrasalVerbs\")]//"
	"span[@role=\"text\" and contains(@class, \"l\")]/text()";

// rhum (also rum)
static const char g_xpath_also_words[] =
	"//span[contains(@class, \"hg\")]/"
	"span[@class=\"vg\"]/span[@class=\"v\"]/text()";

// e.g. for plurals
static const char g_xpath_other_words[] =
	"//span[@class=\"fg\"]/span[@class=\"f\"]/text()";


typedef std::pair<size_t, size_t> ByteRangeT;

struct EntryPosition {
	EntryPosition() : file_range(0, 0),
			  uncompressed_range(0, 0) {}
	EntryPosition(const ByteRangeT &fr,
		      const ByteRangeT &ur
	) : file_range(fr), uncompressed_range(ur) {}

	/// Range of bytes in the compressed file
	ByteRangeT file_range;
	/// Range of bytes in the uncompressed block
	ByteRangeT uncompressed_range;
};

class Entry {
public:
	Entry() {}
	Entry(
		const std::string &name,
		const EntryPosition &pos
	) : _name(name), _pos(pos) {}
	Entry(
		const std::string &name,
		const std::string &content,
		const EntryPosition &pos
	) : _name(name), _content(content), _pos(pos) {}

	/// Case sensitive
	std::string _name;
	/// This is only stored when the index is built. Will be empty after the
	/// index is loaded back from disk.
	std::string _content;
	EntryPosition _pos;
};

/// Key is downcased
typedef std::multimap<std::string, Entry> IndexT;
/// Key and value are downcased
typedef std::map<std::string, std::string> LinksT;
typedef std::pair<IndexT::const_iterator,
		  IndexT::const_iterator> EntryRangeT;
typedef std::unordered_multimap<std::string, std::string> BackLinksT;
typedef std::pair<BackLinksT::const_iterator,
		  BackLinksT::const_iterator> BackLinksRangeT;


/// Return true if 'x' starts with 's'
static inline bool startswith(const std::string &x, const char * const s) {
	const std::size_t lx = x.size(), ls = strlen(s);
	return lx >= ls && !x.compare(0, ls, s);
}

/// Return true if 'x' ends with 's'
static inline bool endswith(const std::string &x, const char * const s) {
	const std::size_t lx = x.size(), ls = strlen(s);
	return lx >= ls && !x.compare(lx-ls, ls, s);
}

void strip(std::string &s) {

	if (s.empty()) {
		return;
	}

	// left
	{
		std::string::iterator it = s.begin();
		while (it != s.end() && std::isspace(static_cast<unsigned char>(*it))) {
			++it;
		}
		s.erase(s.begin(), it);
	}

	// right
	{
		std::string::reverse_iterator it = s.rbegin();
		while (it != s.rend() && std::isspace(static_cast<unsigned char>(*it))) {
			++it;
		}
		s.erase(it.base(), s.end());
	}
}

static inline void downcase(std::string &name) {
	for (	std::string::iterator
		kt=name.begin(); kt!=name.end(); ++kt
	) {
		const char c = *kt;
		if (c >= 'A' && c <= 'Z') {
			*kt = c-'A'+'a';
		}
	}
}


#define BUF_SIZE 16384
static std::vector<unsigned char> g_decompress_buf;

// http://zlib.net/zlib_how.html
static int decompress_it(
	const unsigned char *in,
	size_t nbytes,
	const unsigned char **next,
	std::string &sink
) {
	if (g_decompress_buf.empty()) {
		g_decompress_buf.resize(BUF_SIZE);
	}
	unsigned char * const out = &g_decompress_buf[0];

	z_stream zst;
	memset(&zst, 0, sizeof(zst));
	int ret = inflateInit(&zst);
	if (ret != Z_OK) {
		return ret;
	}

	do {
		const size_t next_nbytes = std::min((size_t)BUF_SIZE, nbytes);
		if (next_nbytes == 0) {
			break;
		}
		zst.avail_in = next_nbytes;
		zst.next_in = const_cast<unsigned char*>(in);

		in     += next_nbytes;
		nbytes -= next_nbytes;

		do {
			zst.avail_out = BUF_SIZE;
			zst.next_out = out;

			ret = inflate(&zst, Z_NO_FLUSH);
			assert(ret != Z_STREAM_ERROR);
			switch (ret) {
			case Z_NEED_DICT:
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				inflateEnd(&zst);
				return ret;
			}

			const unsigned int have = BUF_SIZE - zst.avail_out;
			for (unsigned int i=0; i<have; ++i) {
				sink.push_back(out[i]);
			}

		} while (zst.avail_out == 0);

	} while (ret != Z_STREAM_END);

	if (next) {
		*next = zst.next_in;
	}

	inflateEnd(&zst);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

static int eval_xpath(
	xmlDocPtr doc,
	const char * const xpath,
	std::set<std::string> &out
) {
	int ret = 0;
	xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
	ctx->node = xmlDocGetRootElement(doc);
	xmlXPathObjectPtr obj = xmlXPathEvalExpression((xmlChar*)xpath, ctx);
	if (obj) {
		xmlNodeSetPtr nodeset = obj->nodesetval;
		if (!xmlXPathNodeSetIsEmpty(nodeset)) {
			const int len = xmlXPathNodeSetGetLength(nodeset);
			for (int i=0; i<len; ++i){
				xmlNodePtr node = xmlXPathNodeSetItem(nodeset, i);
				xmlChar *content = xmlNodeGetContent(node);
				if (content) {
					out.insert((char*)content);
					xmlFree(content);
				}
			}
		} else {
			ret = 1;
		}
		xmlXPathFreeObject(obj);
	} else {
		ret = 1;
	}
	xmlXPathFreeContext(ctx);
	return ret;
}

static int name_from_entry(
	const std::string &entry_text,
	std::string &name
) {
	name.clear();
	xmlDocPtr doc = xmlParseDoc((const xmlChar*)entry_text.c_str());
	if (!doc) {
		return 1;
	}
	xmlNodePtr root = xmlDocGetRootElement(doc);
	if (!root) {
		xmlFreeDoc(doc);
		return 1;
	}
	xmlNsPtr ns = xmlSearchNs(doc, root, (const xmlChar*)"d");
	if (!ns) {
		xmlFreeDoc(doc);
		return 1;
	}
	xmlChar * const title = xmlGetProp(root, (const xmlChar*)"title");
	if (!title) {
		xmlFreeDoc(doc);
		return 1;
	}
	name = (const char *)title;
	xmlFree(title);
	xmlFreeDoc(doc);
	return 0;
}

/// Returns true if we've reached the end and parsing should stop
static bool build_index(
	const std::string &input,
	IndexT &index,
	const ByteRangeT &file_range
) {
	std::string::size_type pos = 4;
	std::string entry_text, name, key;

	while (true) {

		const std::string::size_type eol = input.find('\n', pos);
		if (eol == std::string::npos) {
			break;
		}
		entry_text.assign(input, pos, eol-pos);

		if (	!startswith(entry_text, "<d:entry") ||
			!endswith(entry_text, "</d:entry>") ||
			name_from_entry(entry_text, name) ||
			name.empty()
		) {
			return true;
		}

		key = name;
		downcase(key);

		index.insert(IndexT::value_type(
				     key,
				     Entry(name, entry_text,
					   EntryPosition(file_range, ByteRangeT(pos, eol)))));

		// skip bytes between entries
		pos = eol + 5;
	}

	return false;
}

static void read_all_entries(
	size_t input,
	const std::string &content,
	IndexT &index
) {
	const std::ios_base::fmtflags flags = cerr.flags();
	const std::streamsize prec = cerr.precision();

	const size_t total_bytes = content.size();

	std::string out;

	for (size_t i=0; input<total_bytes; ++i) {

		const unsigned char * const cur =
			reinterpret_cast<const unsigned char*>(&content[input]);
		const unsigned char *next = NULL;
		const size_t remain = total_bytes - input;

		if (remain == 0) {
			break;
		}

		out.clear();
		if (Z_OK == decompress_it(cur, remain, &next, out)) {
			if (!next) {
				break;
			}
			if (build_index(out, index, ByteRangeT(input, input+(next-cur)))) {
				break;
			}

			if (i % 50 == 0) {
				cerr << std::setprecision(2) << std::fixed <<
					((float(input)/total_bytes)*100) << "%\t" <<
					std::setprecision(prec) <<
					index.size() << " entries\n";
			}

			input += next-cur;
		} else {
			// error, skip ahead until we find valid compressed block
			++input;
		}
	}

	cerr.flags(flags);
}

static int read_one_entry(
	std::ifstream &infile,
	const EntryPosition &pos,
	std::string &entry_text
) {
	entry_text.clear();

	std::string content;

	// read range from compressed file
	{
		const ByteRangeT r = pos.file_range;
		const size_t nbytes = r.second - r.first;
		infile.seekg(r.first);
		content.resize(nbytes);
		if (!infile.read(&content[0], nbytes)) {
			cerr << "failed to read byte range [" << r.first << ", " << r.second << ")\n";
			return 1;
		}

		if (!entry_text.capacity()) {
			entry_text.reserve(nbytes);
		}
	}

	if (Z_OK == decompress_it(
		    reinterpret_cast<const unsigned char*>(&content[0]),
		    content.size(), NULL, entry_text)
	) {
		// keep range in uncompressed block corresponding to the entry
		const size_t nbytes = entry_text.size();
		const ByteRangeT r = pos.uncompressed_range;
		if (	r.first > nbytes ||
			r.second > nbytes
		) {
			cerr << "uncompressed block is " << nbytes << " bytes, "
				"entry [" << r.first << ", " << r.second << ") was out-of-range\n";
			return 1;
		}
		entry_text.erase(r.second, nbytes-r.second);
		entry_text.erase(0, r.first);
	} else {
		cerr << "failed to decompress entry from file range [" <<
			pos.file_range.first << ", " <<
			pos.file_range.second << ")\n";
		return 1;
	}

	return 0;
}

struct FindLinks {
public:
	FindLinks(
		const IndexT &index,
		LinksT &links,
		BackLinksT &backlinks
	) : _index(index),
	    _links(links),
	    _backlinks(backlinks) {}

	/// 'r' is the range of index entries for a single word. Some words have
	/// multiple definitions.
	void operator()(const EntryRangeT &r) {
		if (r.first == r.second) {
			return;
		}
		const std::string &key = r.first->first;

		xmlDocPtr doc = parse_entry_for_links(r);
		if (!doc) {
			return;
		}

		_words.clear();
		find_words(doc, g_xpath_also_words);

		// other spellings and abbreviations
		for (const std::string &w : _words) {
			if (w != key && _index.find(w) != _index.end()) {
				// e.g. rum -> rhum
				_backlinks.insert(BackLinksT::value_type(w, key));
			}
		}

		find_words(doc, g_xpath_derivatives);
		find_words(doc, g_xpath_other_words);
		find_words(doc, g_xpath_phrases);
		find_words(doc, g_xpath_phrases_other);
		find_words(doc, g_xpath_phrasal_verbs);
		_words.erase(key);

		xmlFreeDoc(doc);

		for (const std::string &w : _words) {
			if (_index.find(w) != _index.end()) {
				continue;
			}
			_links.insert(LinksT::value_type(w, key));
		}

	}

private:
	std::set<std::string> _tmp, _words;
	const IndexT &_index;
	LinksT &_links;
	BackLinksT &_backlinks;

	void find_words(xmlDocPtr doc, const char * const xpath) {
		_tmp.clear();
		eval_xpath(doc, xpath, _tmp);
		for (const std::string &t : _tmp) {
			std::string s(t);
			strip(s);
			downcase(s);
			_words.insert(s);
		}
	}

	/// Caller must free returned object
	static xmlDocPtr parse_entry_for_links(const EntryRangeT &r) {
		const size_t num = std::distance(r.first, r.second);
		if (num > 1) {
			std::string content("<div>");
			for (	IndexT::const_iterator
				it=r.first; it!=r.second; ++it
			) {
				content += it->second._content;
			}
			content += "</div>";

			return xmlParseDoc((xmlChar*)content.c_str());

		} else if (num == 1) {
			const std::string &content = r.first->second._content;
			return xmlParseDoc((xmlChar*)content.c_str());
		}
		return NULL;
	}

	// non-copyable
	FindLinks(const FindLinks &);
	FindLinks &operator=(const FindLinks &);
};


static inline bool file_exists(const char * const fn) {
	struct stat s;
	return 0 == stat(fn, &s) && (S_ISREG(s.st_mode));
}

static inline void write_string(const std::string &s, std::ostream &out) {
	unsigned int n = s.size();
	out.write((const char*)&n, sizeof(n));
	out.write(s.c_str(), n);
}

static inline std::istream &read_string(std::string &s, std::istream &in) {
	unsigned int n;
	if (in.read((char*)&n, sizeof(n))) {
		s.resize(n);
		in.read((char*)&s[0], n);
	}
	return in;
}

static void write_index(
	const IndexT &index,
	const LinksT &links,
	const BackLinksT &backlinks,
	std::ostream &out
) {
	out.write("DICT", 4);

	unsigned char version = 1;
	out.write((const char*)&version, sizeof(version));

	size_t n = index.size();
	out.write((const char*)&n, sizeof(n));
	for (	IndexT::const_iterator
		it=index.begin(); it!=index.end(); ++it
	) {
		write_string(it->first, out);
		write_string(it->second._name, out);

		const EntryPosition &pos = it->second._pos;
		out.write((const char*)&pos.file_range.first,	       sizeof(size_t));
		out.write((const char*)&pos.file_range.second,	       sizeof(size_t));
		out.write((const char*)&pos.uncompressed_range.first,  sizeof(size_t));
		out.write((const char*)&pos.uncompressed_range.second, sizeof(size_t));
	}

	n = links.size();
	out.write((const char*)&n, sizeof(n));
	for (	LinksT::const_iterator
		it=links.begin(); it!=links.end(); ++it
	) {
		write_string(it->first, out);
		write_string(it->second, out);
	}

	n = backlinks.size();
	out.write((const char*)&n, sizeof(n));
	for (	BackLinksT::const_iterator
		it=backlinks.begin(); it!=backlinks.end(); ++it
	) {
		write_string(it->first, out);
		write_string(it->second, out);
	}
}

static int read_index(
	IndexT &index,
	LinksT &links,
	BackLinksT &backlinks,
	std::istream &in
) {

	char magic[5];
	if (!in.read(magic, 4)) {
		return 1;
	}
	magic[4] = '\0';
	if (strcmp(magic, "DICT")) {
		cerr << "Expecting file magic to be DICT\n";
		return 1;
	}

	unsigned char version = 0;
	if (!in.read((char*)&version, sizeof(version))) {
		cerr << "Failed to read index version\n";
		return 1;
	}


	size_t n;
	std::string name, key, val;

	// index
	if (!in.read((char*)&n, sizeof(n))) {
		return 1;
	}
	for (size_t i=0; i<n; ++i) {
		if (!read_string(key, in)) {
			return 1;
		}
		if (!read_string(name, in)) {
			return 1;
		}
		EntryPosition pos;
		if (	!in.read((char*)&pos.file_range.first,		sizeof(size_t)) ||
			!in.read((char*)&pos.file_range.second,		sizeof(size_t)) ||
			!in.read((char*)&pos.uncompressed_range.first,  sizeof(size_t)) ||
			!in.read((char*)&pos.uncompressed_range.second, sizeof(size_t))
		) {
			return 1;
		}
		index.insert(IndexT::value_type(key, Entry(name, pos)));
	}

	// links
	if (!in.read((char*)&n, sizeof(n))) {
		return 1;
	}
	for (size_t i=0; i<n; ++i) {
		if (	!read_string(key, in) |
			!read_string(val, in)
		) {
			return 1;
		}
		links.insert(LinksT::value_type(key, val));
	}

	// backlinks
	if (!in.read((char*)&n, sizeof(n))) {
		return 1;
	}
	for (size_t i=0; i<n; ++i) {
		if (	!read_string(key, in) |
			!read_string(val, in)
		) {
			return 1;
		}
		backlinks.insert(BackLinksT::value_type(key, val));
	}

	return 0;
}

static void concat_entries(
	std::ifstream &infile,
	const EntryRangeT r,
	std::string &out
) {
	std::string entry_text;
	for (	IndexT::const_iterator
		it=r.first; it!=r.second; ++it
	) {
		if (read_one_entry(infile, it->second._pos, entry_text)) {
			continue;
		}
		out += entry_text;
	}
}

static inline EntryRangeT lookup(
	const std::string &w,
	const IndexT &index,
	const LinksT &links
) {
	const EntryRangeT r = index.equal_range(w);
	if (r.first != r.second) {
		return r;
	}
	const LinksT::const_iterator it = links.find(w);
	if (it != links.end()) {
		return index.equal_range(it->second);
	}
	return EntryRangeT(index.end(), index.end());
}

struct DictionaryRef {
	DictionaryRef(
		std::ifstream &infile,
		const std::string &fn,
		const IndexT &index,
		const LinksT &links,
		const BackLinksT &backlinks
	) : _infile(infile),
	    _fn(fn),
	    _index(index),
	    _links(links),
	    _backlinks(backlinks)
		{}

	std::ifstream &_infile;
	const std::string &_fn;
	const IndexT &_index;
	const LinksT &_links;
	const BackLinksT &_backlinks;
};

void output_color_css(const char *text, const char *background, std::ostream &out) {
	out <<
		"  color: " << text << ";\n"
		"  background-color: " << background << ";\n";
}

void output_body_css(const bool dark, std::ostream &out) {
	out <<
		"body {\n"
		"  font-family: Sans-Serif;\n";
	output_color_css(dark ? "white"   : "black",
			 dark ? "#1d1d1d" : "white", out);
	out << "}\n";
}

int output_definition(
	const DictionaryRef &d,
	const std::string &target,
	const bool embed_default_css,
	const bool dark,
	std::ostream &out,
	std::ostream &err
) {
	out <<
		"<html lang=\"en\">\n"
		"<head>\n"
		"<meta charset=\"utf-8\">\n"
		"<title>Dictionary</title>\n";

	// DefaultStyle.css in the same directory as Body.data
	std::ostringstream css;
	{
		char * const dirc = strdup(d._fn.c_str());
		const char * const dir = dirname(dirc);
		if (!dir) {
			err << "Failed to get dirname from path " << d._fn << "\n";
			free(dirc);
			return 1;
		}
		css << dir << "/DefaultStyle.css";
		free(dirc);
	}

	if (embed_default_css) {
		std::ifstream cssfile(css.str().c_str(), std::ios::binary);
		if (!cssfile.is_open()) {
			err << "Failed to open \"" << css.str() << "\"\n";
			return 1;
		}
		out << "<style>\n";
		out << cssfile.rdbuf();
		out << "</style>\n";
	} else {
		out << "<link rel=\"stylesheet\" href=\"" << css.str() << "\">\n";
	}

	out << "<style>\n";

	output_body_css(dark, out);

	out <<
		".x_xoLblBlk {\n"
		"    border-bottom: 1px solid #cccccc;\n"
		"    padding-bottom: 50px;\n"
		"    color: #888888;\n"
		"}\n"
		".note {\n"
		"    border: 1px solid #cccccc;\n"
		"}\n"
		".reg,.tg_gg,.tg_hw,.sy,.gg,.ex,.sn,.ph,.prx,.tg_vg,.vg {\n"
		"    color: #777777;\n"
		"}\n"
		".v,.bold {\n"
		"    color: " << (dark ? "white" : "black") << ";\n"
		"}\n"
		"</style>\n"
		"</head>\n";

	out << "<body>\n";

	std::string key = target;
	downcase(key);
	const EntryRangeT r = lookup(key, d._index, d._links);
	if (r.first == r.second) {
		err << "No entries found\n";
		return 2;
	}

	std::string content;
	{
		bool multi = std::distance(r.first, r.second) > 1;

		concat_entries(d._infile, r, content);

		const BackLinksRangeT br = d._backlinks.equal_range(key);
		if (br.first != br.second) {
			multi = true;
			for (	BackLinksT::const_iterator
				it=br.first; it!=br.second; ++it
			) {
				// append the other page
				concat_entries(d._infile, d._index.equal_range(it->second), content);
			}
		}

		if (multi) {
			content += "</div>";
			content = std::string("<div>") + content;
		}
	}

	xmlDocPtr doc = xmlParseDoc((xmlChar*)content.c_str());
	if (doc) {
		out << "<div class=\"div-entry\">\n";

		xmlChar *s;
		int size;
		xmlDocDumpMemoryEnc(doc, &s, &size, "UTF-8");
		if (s) {
			out.write((char*)s, size);
			out << "\n";
			xmlFree(s);
		}

		out << "</div>\n";
	}

	out << "</body>\n";

	if (doc) {
		xmlFreeDoc(doc);
		return 0;
	}

	err << "Failed to parse entry for \"" << target << "\"\n";
	return 1;
}

void list_words(
	const DictionaryRef &d,
	const std::string &target,
	void (*func)(const std::string &, void *data),
	void *data
) {
	std::string key = target;
	downcase(key);

	for (	IndexT::const_iterator it=d._index.lower_bound(key);
		it!=d._index.end() && startswith(it->first, key.c_str()); ++it
	) {
		func(it->second._name, data);
	}
	for (	LinksT::const_iterator it=d._links.lower_bound(key);
		it!=d._links.end() && startswith(it->first, key.c_str()); ++it
	) {
		func(it->first, data);
	}
}

static void list_all_words(
	const DictionaryRef &d,
	void (*func)(const std::string &, void *data),
	void *data
) {
	for (	IndexT::const_iterator
		it=d._index.begin(); it!=d._index.end(); ++it
	) {
		func(it->second._name, data);
	}
	for (	LinksT::const_iterator
		it=d._links.begin(); it!=d._links.end(); ++it
	) {
		func(it->first, data);
	}
}

static void usage(const char * const bin) {
	cerr << bin << " [-h] -d /path/to/Body.data [-i index] [-D] [-c] [-a] [[-l | -o out.html] word]\n";
	cerr << "\n";
	cerr << "-h    Print help.\n";
	cerr << "-d    Absolute path to Body.data file. The DefaultStyle.css in the same directory will also be read.\n";
	cerr << "-i    Index cache file to write (if it doesn't exist), otherwise read. Recommended for speed.\n";
	cerr << "-D    Dark mode.\n";
	cerr << "-c    Centre the window on the screen.\n";
	cerr << "-l    List words to stdout for which 'word' is a prefix, instead of starting GUI.\n";
	cerr << "-a    List all words to stdout, one per line, instead of starting GUI.\n";
	cerr << "-o    Output html file containing the definition of 'word', instead of starting GUI.\n";
	cerr << "word  Word to lookup.\n";
}

int main(int argc, char *argv[]) {

	std::string fn, index_cache, target, out_fn;
	bool list = false;
	bool all = false;
	bool dark = false;
	bool centre = false;

	// command line options
	{
		int opt;
		while ((opt = getopt(argc, argv, "hd:i:o:laDc")) != -1) {
			switch (opt) {
			case 'h':
				usage(argv[0]);
				return 0;
			case 'd':
				fn = optarg;
				break;
			case 'i':
				index_cache = optarg;
				break;
			case 'o':
				out_fn = optarg;
				break;
			case 'l':
				list = true;
				break;
			case 'a':
				all = true;
				break;
			case 'D':
			        dark = true;
				break;
			case 'c':
				centre = true;
				break;
			default:
				usage(argv[0]);
				return 1;
			}
		}

		if (fn.empty()) {
			cerr << argv[0] << " : expecting -d Body.data argument\n";
			cerr << argv[0] << " : Run the macDict.sh script instead of the binary directly\n";
			return 1;
		}
		if (!endswith(fn.c_str(), "Body.data")) {
			cerr << argv[0] << " : dictionary file should be Body.data\n";
			return 1;
		}
		if (optind < argc) {
			target = argv[optind];
			strip(target);
		}
	}


	LIBXML_TEST_VERSION
	xmlKeepBlanksDefault(0);

	IndexT index;
	LinksT links;
	BackLinksT backlinks;

	std::ifstream infile(fn.c_str(), std::ios::binary);
	if (!infile.is_open()) {
		cerr << argv[0] << " : failed to open \"" << fn << "\"\n";
		return 1;
	}

	if (index_cache.empty() || !file_exists(index_cache.c_str())) {

		// generate index

		cerr << "Reading " << fn << "\n";

		std::string content;
		{
			infile.seekg(0, std::ios::end);
			content.resize(infile.tellg());
			infile.seekg(0, std::ios::beg);
			if (!infile.read(&content[0], content.size())) {
				cerr << argv[0] << " : failed to read " << content.size() << " bytes from \"" << fn << "\"\n";
				return 1;
			}
		}

		read_all_entries(100, content, index);

		cerr << index.size() << " index entries\n";

		cerr << "Finding links...\n";

		FindLinks find_links(index, links, backlinks);
		{
			const std::ios_base::fmtflags flags = cerr.flags();
			const std::streamsize prec = cerr.precision();
			const size_t index_size = index.size();
			size_t i = 0, k = 0;

			IndexT::const_iterator last, it;
			for (last = it = index.begin(); it!=index.end(); ++it, ++i) {
				if (last->first != it->first) {
					find_links(EntryRangeT(last, it));
					last = it;

					if (k++ % 2000 == 0) {
						cerr << std::setprecision(2) << std::fixed <<
							((float(i)/index_size)*100) << "%\t" <<
							std::setprecision(prec) <<
							links.size() << " links\n";
					}
				}
			}
			find_links(EntryRangeT(last, it));

			cerr.flags(flags);
		}

		cerr << links.size() << " links\n";
		cerr << backlinks.size() << " backlinks\n";

		if (!index_cache.empty()) {
			// save index
			cerr << "Writing index to \"" << index_cache << "\"\n";
			std::ofstream outfile(
				index_cache.c_str(),
				std::ios::out|std::ios::trunc|std::ios::binary);
			if (!outfile.is_open()) {
				cerr << argv[0] << " : failed to write index cache to \"" << index_cache << "\"\n";
			} else {
				write_index(index, links, backlinks, outfile);
			}
		}

	} else {
		// load index
		std::ifstream idxfile(index_cache.c_str(), std::ios::binary);
		if (!idxfile.is_open()) {
			cerr << argv[0] << " : failed to open index cache \"" << index_cache << "\"\n";
			return 1;
		}
		if (read_index(index, links, backlinks, idxfile)) {
			cerr << argv[0] << " : failed to read index cache \"" << index_cache << "\"\n";
			return 1;
		}
		if (index.empty()) {
			cerr << argv[0] << " : index was empty after load from \"" << index_cache << "\"\n";
			return 1;
		}
	}

	const DictionaryRef dict(infile, fn, index, links, backlinks);
	int res = 0;

	do {
		if (all) {
			list_all_words(dict,
				[](const std::string &word, void *data) {
					cout << word << "\n";
				}, NULL);
			break;
		}

		if (!target.empty()) {

			if (list) {
				// list entries for which target (downcased) is a prefix

				unsigned int num_found = 0U;

				list_words(dict, target,
					[](const std::string &word, void *data) {
						unsigned int &num_found = *((unsigned int*)data);
						cout << word << "\n";
						++num_found;
					}, &num_found);

				cerr << num_found << " found\n";
				break;

			} else if (!out_fn.empty()) {

				std::ofstream outfile(out_fn.c_str(), std::ios::out|std::ios::trunc);
				if (!outfile.is_open()) {
					cerr << argv[0] << " : failed to create output file " << out_fn << "\n";
					return 1;
				}

				res = output_definition(dict, target, false, dark, outfile, cerr);
				break;
			}
		}

#ifdef WANT_GUI
		QApplication app(argc, argv);

		Window * const w = new Window(dict, dark, target);
		w->resize(850, 600);

		if (centre) {
			QScreen * const s = app.primaryScreen();
			if (s) {
				const QRect sg = s->geometry();
				const QRect fg = w->frameGeometry();
				w->setGeometry((sg.width()-fg.width())/2,
					       (sg.height()-fg.height())/2,
					       w->width(), w->height());
			}
		}

		w->show();

		return app.exec();
#endif

	} while (0);


	xmlCleanupParser();

	return res;
}
