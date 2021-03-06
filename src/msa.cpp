#include "msa.hpp"
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

extern "C" {
#include <libpll/pll_msa.h>
}

pll_msa_t *parse_msa_file(const std::string &msa_filename) {

  debug_print(EMIT_LEVEL_DEBUG, "attempting to open msa: %s",
              msa_filename.c_str());
  if (pll_phylip_t *fd =
          pll_phylip_open(msa_filename.c_str(), pll_map_generic)) {
    pll_msa_t *pll_msa = nullptr;
    if ((pll_msa = pll_phylip_parse_interleaved(fd)) ||
        (pll_msa = pll_phylip_parse_sequential(fd))) {
      pll_phylip_close(fd);
      return pll_msa;
    } else {
      pll_phylip_close(fd);
    }
  }

  if (pll_fasta_t *fd = pll_fasta_open(msa_filename.c_str(), pll_map_fasta)) {
    char *label = nullptr;
    char *sequence = nullptr;
    long sequence_len = 0;
    long header_len = 0;
    long sequence_number = 0;
    long expected_sequence_length = 0;

    std::vector<char *> sequences;
    std::vector<char *> labels;

    while (pll_fasta_getnext(fd, &label, &header_len, &sequence, &sequence_len,
                             &sequence_number)) {
      if (expected_sequence_length == 0) {
        expected_sequence_length = sequence_len;
      } else if (expected_sequence_length != sequence_len) {
        throw std::invalid_argument("Sequences don't match in size");
      }
      sequences.push_back(sequence);
      labels.push_back(label);
    }
    pll_fasta_close(fd);
    pll_msa_t *pll_msa = (pll_msa_t *)malloc(sizeof(pll_msa_t));

    if (sequences.size() >
        static_cast<size_t>(std::numeric_limits<int>::max())) {
      throw std::runtime_error(
          "The size of the sequence is too large to cast safely");
    }

    if (expected_sequence_length >
        static_cast<long int>(std::numeric_limits<int>::max())) {
      throw std::runtime_error(
          "The expected size of the sequence is too large to cast safely");
    }

    pll_msa->count = static_cast<int>(sequences.size());
    pll_msa->length = static_cast<int>(expected_sequence_length);
    if (pll_msa->count < 0) {
      throw std::runtime_error("The MSA had a negative count (overflow?)");
    }
    pll_msa->sequence = (char **)malloc(
        sizeof(char *) * static_cast<unsigned int>(pll_msa->count));
    pll_msa->label = (char **)malloc(sizeof(char *) *
                                     static_cast<unsigned int>(pll_msa->count));
    for (size_t i = 0; i < sequences.size(); ++i) {
      pll_msa->sequence[i] = sequences[i];
      pll_msa->label[i] = labels[i];
    }
    return pll_msa;
  }
  throw std::invalid_argument("Could not parse msa file");
}

static std::string::const_iterator expect_next(std::string::const_iterator itr,
                                               char c) {
  while (std::isspace(*itr)) {
    itr++;
  }
  if (c != *itr) {
    throw std::runtime_error(
        std::string("Failed to parse partition file, expected '") + c +
        "' got '" + *itr + "' instead");
  }
  ++itr;
  while (std::isspace(*itr)) {
    itr++;
  }
  return itr;
}

partition_info_t parse_partition_info(const std::string &line) {
  partition_info_t pi;
  auto itr = line.begin();

  /* skip spaces */
  while (std::isspace(*itr)) {
    itr++;
  }

  /* parse model name */
  auto start = itr;
  while (std::isalnum(*itr)) {
    itr++;
  }
  pi.model_name = std::string(start, itr);

  /* check for comma */
  itr = expect_next(itr, ',');

  /* parse partition name */
  start = itr;
  while (std::isalnum(*itr) || *itr == '_') {
    itr++;
  }
  pi.partition_name = std::string(start, itr);

  /* check for = */
  itr = expect_next(itr, '=');

  do {
    if (*itr == ',')
      ++itr;
    while (std::isspace(*itr)) {
      itr++;
    }

    /* parse begin */
    size_t begin = 0;
    size_t end = 0;
    start = itr;
    while (std::isdigit(*itr)) {
      itr++;
    }
    try {
      auto tmp = std::stol(std::string(start, itr));
      if (tmp < 0) {
        throw std::runtime_error(
            "There was a negative number in the partition specification");
      }
      begin = static_cast<size_t>(tmp);
    } catch (...) {
      throw std::runtime_error(
          std::string("Failed to parse beginning partition number") +
          std::string(start, itr) + " start: '" + *start + "', itr: '" + *itr +
          "'");
    }

    /* check for - */
    itr = expect_next(itr, '-');

    /* parse begin */
    start = itr;
    while (std::isdigit(*itr)) {
      itr++;
    }
    try {
      auto tmp = std::stol(std::string(start, itr));
      if (tmp < 0) {
        throw std::runtime_error(
            "There was a negative number in the partition specification");
      }
      end = static_cast<size_t>(tmp);
    } catch (...) {
      throw std::runtime_error(
          std::string("Failed to parse beginning partition number") +
          std::string(start, itr) + " start: '" + *start + "', itr: '" + *itr +
          "'");
    }
    if (end < begin) {
      throw std::runtime_error(std::string("The end index of the partition '") +
                               pi.partition_name +
                               "' comes before the beginning");
    }
    pi.parts.emplace_back(begin, end);
    while (std::isspace(*itr)) {
      itr++;
    }
  } while (*itr == ',');

  if (pi.model_name.empty()) {
    throw std::runtime_error("Error, partition is missing a model name");
  }

  return pi;
}

/* Grammer:
 * <MODEL_NAME> , <PARTITION_NAME> = <BEGIN> - <END>[, <BEGIN> - <END>]*
 */
msa_partitions_t parse_partition_file(const std::string &filename) {
  std::ifstream partition_file{filename};
  msa_partitions_t parts;
  for (std::string line; std::getline(partition_file, line);) {
    parts.push_back(parse_partition_info(line));
  }
  return parts;
}

msa_t::msa_t(const msa_t &other, const partition_info_t &partition) {
  _msa = (pll_msa_t *)malloc(sizeof(pll_msa_t));
  _msa->count = other.count();
  _msa->length = 0;
  for (auto range : partition.parts) {
    /*
     * Since the range specification is [first, second], we have to add one to
     * include the endpoint
     */
    size_t cur_partition_length = (range.second - range.first) + 1;
    if (cur_partition_length >
        static_cast<size_t>(std::numeric_limits<int>::max())) {
      throw std::runtime_error("Partition range is too large to cast safely");
    }
    _msa->length += static_cast<int>(cur_partition_length);
    debug_print(EMIT_LEVEL_DEBUG, "%d", _msa->length);
  }

  if (other.count() < 0) {
    throw std::runtime_error("MSA from which we are copy constructing has a "
                             "negative count (overflow?)");
  }

  _msa->sequence = (char **)malloc(static_cast<size_t>(sizeof(char *)) *
                                   static_cast<size_t>(other.count()));

  for (int i = 0; i < other.count(); ++i) {
    if (_msa->length <= 0) {
      throw std::runtime_error(
          "The size of the MSA is less than 0 (overflow?)");
    }

    // We need to make space for the terminating zero
    _msa->sequence[i] = (char *)malloc(static_cast<size_t>(sizeof(char)) *
                                       static_cast<size_t>(_msa->length + 1));
    size_t cur_idx = 0;
    char *other_sequence = other.sequence(i);
    for (auto range : partition.parts) {
      for (size_t j = range.first; j <= range.second; ++j) {
        _msa->sequence[i][cur_idx++] = other_sequence[j];
      }
    }
  }

  if (_msa->count < 0) {
    throw std::runtime_error(
        "The current MSA count is less than 0 (overflow?)");
  }
  _msa->label =
      (char **)calloc(sizeof(char *), static_cast<size_t>(_msa->count));
  for (int i = 0; i < _msa->count; ++i) {
    size_t label_size = strlen(other.label(i)) + 1;
    _msa->label[i] = (char *)malloc(sizeof(char) * label_size);
    strncpy(_msa->label[i], other.label(i), label_size);
  }
  _map = other.map();
  _states = other.states();
  _weights = nullptr;
  compress();
}

char *msa_t::sequence(int index) const {
  if (index < _msa->count && !(index < 0))
    return _msa->sequence[index];
  throw std::out_of_range("Requested sequence does not exist");
}

char *msa_t::label(int index) const {
  if (index < _msa->count && !(index < 0))
    return _msa->label[index];
  throw std::out_of_range("Requested label does not exist");
}

unsigned int *msa_t::weights() const {
  if (_weights)
    return _weights;
  throw std::runtime_error("msa_t has no weights");
}

unsigned int msa_t::total_weight() const {
  unsigned int total = 0;
  for (int i = 0; i < _msa->length; ++i) {
    total += _weights[i];
  }
  return total;
}

const pll_state_t *msa_t::map() const { return _map; }

unsigned int msa_t::states() const { return _states; }

int msa_t::count() const { return _msa->count; }

unsigned int msa_t::length() const {
  return static_cast<unsigned int>(_msa->length);
}

void msa_t::compress() {
  if (_weights != nullptr) {
    free(_weights);
  }
  int new_length = _msa->length;
  _weights = pll_compress_site_patterns(_msa->sequence, _map, _msa->count,
                                        &new_length);
  _msa->length = new_length;
}

std::vector<msa_t> msa_t::partition(const msa_partitions_t &ps) const {
  std::vector<msa_t> parted_msa;
  parted_msa.reserve(ps.size());
  for (auto p : ps) {
    parted_msa.emplace_back(*this, p);
  }
  return parted_msa;
}

bool msa_t::constiency_check(std::unordered_set<std::string> labels) const {
  std::unordered_set<std::string> taxa;
  for (int i = 0; i < _msa->count; ++i) {
    taxa.insert(_msa->label[i]);
  }

  bool ret = true;

  // labels subset taxa
  for (const std::string &k : labels) {
    if (taxa.find(k) == taxa.end()) {
      debug_print(EMIT_LEVEL_DEBUG, "Taxa %s in msa is not present on the tree",
                  k.c_str());
      ret = false;
    }
  }

  // taxa subset labels
  for (const std::string &k : taxa) {
    if (labels.find(k) == labels.end()) {
      debug_print(EMIT_LEVEL_DEBUG, "Taxa %s on tree is not present in the msa",
                  k.c_str());
      ret = false;
    }
  }
  return ret;
}

msa_t::~msa_t() {
  if (_msa)
    pll_msa_destroy(_msa);
  if (_weights)
    free(_weights);
}
