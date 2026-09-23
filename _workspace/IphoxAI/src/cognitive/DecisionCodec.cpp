#include "iphox/cognitive/DecisionCodec.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace iphox::cognitive {
namespace {

constexpr std::array<std::byte, 4> kRequestMagic{
    std::byte{'D'}, std::byte{'Q'}, std::byte{'R'}, std::byte{'1'}
};

constexpr std::array<std::byte, 4> kResponseMagic{
    std::byte{'D'}, std::byte{'R'}, std::byte{'S'}, std::byte{'1'}
};

constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kMaxEncodedBytes = 1024u * 1024u;
constexpr std::size_t kMaxStringBytes = 64u * 1024u;
constexpr std::size_t kMaxStateEntries = 128;
constexpr std::size_t kMaxQuestions = 64;
constexpr std::size_t kMaxOptions = 1024;
constexpr std::size_t kMaxAnswers = 64;

enum class WireQuestionType : std::uint8_t {
    Noul = 1,
    Choice = 2,
    Score = 3
};

enum class WireAnswerType : std::uint8_t {
    Noul = 1,
    Choice = 2,
    Score = 3,
    Abstain = 4
};

template <typename T>
void AppendIntegral(
    std::vector<std::byte>& out,
    T value) {

    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    const U raw = static_cast<U>(value);

    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(
            static_cast<std::byte>(
                (raw >> (i * 8)) & U{0xFF}));
    }
}

template <typename T>
bool ReadIntegral(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    T& value) {

    static_assert(std::is_integral_v<T>);

    if (offset + sizeof(T) > bytes.size()) {
        return false;
    }

    using U = std::make_unsigned_t<T>;
    U raw{};

    for (std::size_t i = 0; i < sizeof(T); ++i) {
        raw |= static_cast<U>(
            std::to_integer<unsigned int>(
                bytes[offset + i]))
            << (i * 8);
    }

    value = static_cast<T>(raw);
    offset += sizeof(T);
    return true;
}

void AppendDouble(
    std::vector<std::byte>& out,
    double value) {

    AppendIntegral(
        out,
        std::bit_cast<std::uint64_t>(value));
}

bool ReadDouble(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    double& value) {

    std::uint64_t raw{};
    if (!ReadIntegral(bytes, offset, raw)) {
        return false;
    }

    value = std::bit_cast<double>(raw);
    return true;
}

void AppendString(
    std::vector<std::byte>& out,
    const std::string& value) {

    if (value.size() > kMaxStringBytes ||
        value.size() >
            std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(
            "DecisionCodec string too large");
    }

    AppendIntegral(
        out,
        static_cast<std::uint32_t>(
            value.size()));

    for (const unsigned char ch : value) {
        out.push_back(
            static_cast<std::byte>(ch));
    }
}

bool ReadString(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    std::string& value) {

    std::uint32_t size{};
    if (!ReadIntegral(bytes, offset, size)) {
        return false;
    }

    if (size > kMaxStringBytes ||
        offset + size > bytes.size()) {
        return false;
    }

    value.clear();
    value.reserve(size);

    for (std::uint32_t i = 0; i < size; ++i) {
        value.push_back(
            static_cast<char>(
                std::to_integer<unsigned char>(
                    bytes[offset + i])));
    }

    offset += size;
    return true;
}

template <typename T>
CodecResult<T> Fail(
    DecisionCodecStatus status,
    std::string error) {

    CodecResult<T> result;
    result.status = status;
    result.error = std::move(error);
    return result;
}

bool CheckMagic(
    std::span<const std::byte> bytes,
    const std::array<std::byte, 4>& magic) {

    if (bytes.size() < magic.size()) {
        return false;
    }

    for (std::size_t i = 0; i < magic.size(); ++i) {
        if (bytes[i] != magic[i]) {
            return false;
        }
    }

    return true;
}

void CheckSize(
    const std::vector<std::byte>& bytes) {

    if (bytes.size() > kMaxEncodedBytes) {
        throw std::invalid_argument(
            "DecisionCodec payload too large");
    }
}

void AppendOptionalConfidence(
    std::vector<std::byte>& out,
    const std::optional<double>& confidence) {

    AppendIntegral<std::uint8_t>(
        out,
        confidence.has_value() ? 1 : 0);

    if (confidence.has_value()) {
        AppendDouble(out, *confidence);
    }
}

bool ReadOptionalConfidence(
    std::span<const std::byte> bytes,
    std::size_t& offset,
    std::optional<double>& confidence) {

    std::uint8_t present{};
    if (!ReadIntegral(bytes, offset, present) ||
        present > 1) {
        return false;
    }

    if (present == 0) {
        confidence.reset();
        return true;
    }

    double value{};
    if (!ReadDouble(bytes, offset, value)) {
        return false;
    }

    confidence = value;
    return true;
}

} // namespace

std::vector<std::byte> DecisionCodec::EncodeRequest(
    const DecisionRequest& request) {

    if (request.state.size() > kMaxStateEntries ||
        request.questions.size() > kMaxQuestions) {
        throw std::invalid_argument(
            "DecisionCodec request count limit exceeded");
    }

    std::vector<std::byte> out;
    out.insert(
        out.end(),
        kRequestMagic.begin(),
        kRequestMagic.end());

    AppendIntegral(out, kVersion);
    AppendString(out, request.schemaId);
    AppendIntegral(out, request.schemaVersion);

    AppendIntegral(
        out,
        static_cast<std::uint16_t>(
            request.state.size()));

    for (const auto& [key, value] : request.state) {
        AppendString(out, key);
        AppendString(out, value);
    }

    AppendIntegral(
        out,
        static_cast<std::uint16_t>(
            request.questions.size()));

    for (const auto& [id, question] : request.questions) {
        AppendString(out, id);

        if (const auto* noul =
                std::get_if<NoulQuestion>(&question)) {

            AppendIntegral(
                out,
                static_cast<std::uint8_t>(
                    WireQuestionType::Noul));
            AppendString(out, noul->instructions);
            AppendIntegral<std::uint8_t>(
                out,
                noul->criteria.has_value() ? 1 : 0);

            if (noul->criteria.has_value()) {
                AppendString(
                    out,
                    noul->criteria->trueCriteria);
                AppendString(
                    out,
                    noul->criteria->falseCriteria);
            }

        } else if (const auto* choice =
                       std::get_if<ChoiceQuestion>(&question)) {

            if (choice->options.size() > kMaxOptions) {
                throw std::invalid_argument(
                    "DecisionCodec Choice option limit exceeded");
            }

            AppendIntegral(
                out,
                static_cast<std::uint8_t>(
                    WireQuestionType::Choice));
            AppendString(out, choice->instructions);
            AppendIntegral(
                out,
                static_cast<std::uint16_t>(
                    choice->options.size()));

            for (const auto& option : choice->options) {
                AppendString(out, option.id);
                AppendString(out, option.criteria);
            }

        } else if (const auto* score =
                       std::get_if<ScoreQuestion>(&question)) {

            if (score->levels.size() > kMaxOptions) {
                throw std::invalid_argument(
                    "DecisionCodec Score level limit exceeded");
            }

            AppendIntegral(
                out,
                static_cast<std::uint8_t>(
                    WireQuestionType::Score));
            AppendString(out, score->instructions);
            AppendIntegral(
                out,
                static_cast<std::uint16_t>(
                    score->levels.size()));

            for (const auto& level : score->levels) {
                AppendString(out, level.criteria);
            }
        }
    }

    CheckSize(out);
    return out;
}

CodecResult<DecisionRequest> DecisionCodec::DecodeRequest(
    std::span<const std::byte> bytes) {

    if (bytes.size() > kMaxEncodedBytes) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::TooLarge,
            "decision request exceeds maximum size");
    }

    if (bytes.size() < kRequestMagic.size() + sizeof(kVersion)) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::TooShort,
            "decision request too short");
    }

    if (!CheckMagic(bytes, kRequestMagic)) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::BadMagic,
            "invalid decision request magic");
    }

    std::size_t offset = kRequestMagic.size();
    std::uint16_t version{};

    if (!ReadIntegral(bytes, offset, version)) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::TooShort,
            "truncated decision request version");
    }

    if (version != kVersion) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::UnsupportedVersion,
            "unsupported decision request version");
    }

    DecisionRequest request;

    if (!ReadString(bytes, offset, request.schemaId) ||
        !ReadIntegral(bytes, offset, request.schemaVersion)) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::LengthMismatch,
            "invalid decision request header");
    }

    std::uint16_t stateCount{};
    if (!ReadIntegral(bytes, offset, stateCount)) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::LengthMismatch,
            "missing state count");
    }

    if (stateCount > kMaxStateEntries) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::CountLimit,
            "state entry count exceeds limit");
    }

    for (std::uint16_t i = 0; i < stateCount; ++i) {
        std::string key;
        std::string value;

        if (!ReadString(bytes, offset, key) ||
            !ReadString(bytes, offset, value)) {
            return Fail<DecisionRequest>(
                DecisionCodecStatus::LengthMismatch,
                "invalid state entry");
        }

        if (!request.state.emplace(
                std::move(key),
                std::move(value)).second) {
            return Fail<DecisionRequest>(
                DecisionCodecStatus::InvalidValue,
                "duplicate state key");
        }
    }

    std::uint16_t questionCount{};
    if (!ReadIntegral(bytes, offset, questionCount)) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::LengthMismatch,
            "missing question count");
    }

    if (questionCount > kMaxQuestions) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::CountLimit,
            "question count exceeds limit");
    }

    for (std::uint16_t i = 0; i < questionCount; ++i) {
        std::string id;
        std::uint8_t type{};

        if (!ReadString(bytes, offset, id) ||
            !ReadIntegral(bytes, offset, type)) {
            return Fail<DecisionRequest>(
                DecisionCodecStatus::LengthMismatch,
                "invalid question header");
        }

        DecisionQuestion question;

        switch (static_cast<WireQuestionType>(type)) {
        case WireQuestionType::Noul: {
            NoulQuestion value;
            std::uint8_t hasCriteria{};

            if (!ReadString(
                    bytes,
                    offset,
                    value.instructions) ||
                !ReadIntegral(
                    bytes,
                    offset,
                    hasCriteria) ||
                hasCriteria > 1) {
                return Fail<DecisionRequest>(
                    DecisionCodecStatus::InvalidValue,
                    "invalid Noul question");
            }

            if (hasCriteria != 0) {
                NoulCriteria criteria;

                if (!ReadString(
                        bytes,
                        offset,
                        criteria.trueCriteria) ||
                    !ReadString(
                        bytes,
                        offset,
                        criteria.falseCriteria)) {
                    return Fail<DecisionRequest>(
                        DecisionCodecStatus::LengthMismatch,
                        "invalid Noul criteria");
                }

                value.criteria =
                    std::move(criteria);
            }

            question = std::move(value);
            break;
        }

        case WireQuestionType::Choice: {
            ChoiceQuestion value;
            std::uint16_t count{};

            if (!ReadString(
                    bytes,
                    offset,
                    value.instructions) ||
                !ReadIntegral(
                    bytes,
                    offset,
                    count)) {
                return Fail<DecisionRequest>(
                    DecisionCodecStatus::LengthMismatch,
                    "invalid Choice question");
            }

            if (count > kMaxOptions) {
                return Fail<DecisionRequest>(
                    DecisionCodecStatus::CountLimit,
                    "Choice option count exceeds limit");
            }

            value.options.reserve(count);

            for (std::uint16_t j = 0; j < count; ++j) {
                ChoiceOption option;

                if (!ReadString(
                        bytes,
                        offset,
                        option.id) ||
                    !ReadString(
                        bytes,
                        offset,
                        option.criteria)) {
                    return Fail<DecisionRequest>(
                        DecisionCodecStatus::LengthMismatch,
                        "invalid Choice option");
                }

                value.options.push_back(
                    std::move(option));
            }

            question = std::move(value);
            break;
        }

        case WireQuestionType::Score: {
            ScoreQuestion value;
            std::uint16_t count{};

            if (!ReadString(
                    bytes,
                    offset,
                    value.instructions) ||
                !ReadIntegral(
                    bytes,
                    offset,
                    count)) {
                return Fail<DecisionRequest>(
                    DecisionCodecStatus::LengthMismatch,
                    "invalid Score question");
            }

            if (count > kMaxOptions) {
                return Fail<DecisionRequest>(
                    DecisionCodecStatus::CountLimit,
                    "Score level count exceeds limit");
            }

            value.levels.reserve(count);

            for (std::uint16_t j = 0; j < count; ++j) {
                ScoreLevel level;

                if (!ReadString(
                        bytes,
                        offset,
                        level.criteria)) {
                    return Fail<DecisionRequest>(
                        DecisionCodecStatus::LengthMismatch,
                        "invalid Score level");
                }

                value.levels.push_back(
                    std::move(level));
            }

            question = std::move(value);
            break;
        }

        default:
            return Fail<DecisionRequest>(
                DecisionCodecStatus::InvalidType,
                "unknown decision question type");
        }

        if (!request.questions.emplace(
                std::move(id),
                std::move(question)).second) {
            return Fail<DecisionRequest>(
                DecisionCodecStatus::InvalidValue,
                "duplicate question id");
        }
    }

    if (offset != bytes.size()) {
        return Fail<DecisionRequest>(
            DecisionCodecStatus::LengthMismatch,
            "trailing bytes in decision request");
    }

    CodecResult<DecisionRequest> result;
    result.status = DecisionCodecStatus::Ok;
    result.value = std::move(request);
    return result;
}

std::vector<std::byte> DecisionCodec::EncodeResponse(
    const DecisionResponse& response) {

    if (response.answers.size() > kMaxAnswers) {
        throw std::invalid_argument(
            "DecisionCodec answer count limit exceeded");
    }

    std::vector<std::byte> out;
    out.insert(
        out.end(),
        kResponseMagic.begin(),
        kResponseMagic.end());

    AppendIntegral(out, kVersion);
    AppendString(out, response.backendId);
    AppendString(out, response.backendModel);

    AppendIntegral(
        out,
        static_cast<std::uint16_t>(
            response.answers.size()));

    for (const auto& [id, answer] : response.answers) {
        AppendString(out, id);

        if (const auto* noul =
                std::get_if<NoulAnswer>(&answer)) {

            AppendIntegral(
                out,
                static_cast<std::uint8_t>(
                    WireAnswerType::Noul));
            AppendDouble(
                out,
                noul->yesProbability);

        } else if (const auto* choice =
                       std::get_if<ChoiceAnswer>(&answer)) {

            if (choice->probabilities.size() > kMaxOptions) {
                throw std::invalid_argument(
                    "DecisionCodec Choice probability limit exceeded");
            }

            AppendIntegral(
                out,
                static_cast<std::uint8_t>(
                    WireAnswerType::Choice));
            AppendString(out, choice->selected);
            AppendIntegral(
                out,
                static_cast<std::uint16_t>(
                    choice->probabilities.size()));

            for (const auto& probability :
                 choice->probabilities) {
                AppendString(
                    out,
                    probability.id);
                AppendDouble(
                    out,
                    probability.value);
            }

            AppendOptionalConfidence(
                out,
                choice->reportedConfidence);

        } else if (const auto* score =
                       std::get_if<ScoreAnswer>(&answer)) {

            if (score->probabilities.size() > kMaxOptions) {
                throw std::invalid_argument(
                    "DecisionCodec Score probability limit exceeded");
            }

            AppendIntegral(
                out,
                static_cast<std::uint8_t>(
                    WireAnswerType::Score));
            AppendDouble(
                out,
                score->expectedScore);
            AppendIntegral(
                out,
                static_cast<std::uint16_t>(
                    score->probabilities.size()));

            for (const auto probability :
                 score->probabilities) {
                AppendDouble(
                    out,
                    probability);
            }

            AppendOptionalConfidence(
                out,
                score->reportedConfidence);

        } else if (const auto* abstain =
                       std::get_if<AbstainAnswer>(&answer)) {

            AppendIntegral(
                out,
                static_cast<std::uint8_t>(
                    WireAnswerType::Abstain));
            AppendString(
                out,
                abstain->reason);
        }
    }

    CheckSize(out);
    return out;
}

CodecResult<DecisionResponse> DecisionCodec::DecodeResponse(
    std::span<const std::byte> bytes) {

    if (bytes.size() > kMaxEncodedBytes) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::TooLarge,
            "decision response exceeds maximum size");
    }

    if (bytes.size() < kResponseMagic.size() + sizeof(kVersion)) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::TooShort,
            "decision response too short");
    }

    if (!CheckMagic(bytes, kResponseMagic)) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::BadMagic,
            "invalid decision response magic");
    }

    std::size_t offset = kResponseMagic.size();
    std::uint16_t version{};

    if (!ReadIntegral(bytes, offset, version)) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::TooShort,
            "truncated decision response version");
    }

    if (version != kVersion) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::UnsupportedVersion,
            "unsupported decision response version");
    }

    DecisionResponse response;

    if (!ReadString(bytes, offset, response.backendId) ||
        !ReadString(bytes, offset, response.backendModel)) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::LengthMismatch,
            "invalid decision response header");
    }

    std::uint16_t answerCount{};
    if (!ReadIntegral(bytes, offset, answerCount)) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::LengthMismatch,
            "missing answer count");
    }

    if (answerCount > kMaxAnswers) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::CountLimit,
            "answer count exceeds limit");
    }

    for (std::uint16_t i = 0; i < answerCount; ++i) {
        std::string id;
        std::uint8_t type{};

        if (!ReadString(bytes, offset, id) ||
            !ReadIntegral(bytes, offset, type)) {
            return Fail<DecisionResponse>(
                DecisionCodecStatus::LengthMismatch,
                "invalid answer header");
        }

        DecisionAnswer answer;

        switch (static_cast<WireAnswerType>(type)) {
        case WireAnswerType::Noul: {
            NoulAnswer value;
            if (!ReadDouble(
                    bytes,
                    offset,
                    value.yesProbability)) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::LengthMismatch,
                    "invalid Noul answer");
            }
            answer = value;
            break;
        }

        case WireAnswerType::Choice: {
            ChoiceAnswer value;
            std::uint16_t count{};

            if (!ReadString(
                    bytes,
                    offset,
                    value.selected) ||
                !ReadIntegral(
                    bytes,
                    offset,
                    count)) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::LengthMismatch,
                    "invalid Choice answer");
            }

            if (count > kMaxOptions) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::CountLimit,
                    "Choice probability count exceeds limit");
            }

            value.probabilities.reserve(count);

            for (std::uint16_t j = 0; j < count; ++j) {
                Probability probability;

                if (!ReadString(
                        bytes,
                        offset,
                        probability.id) ||
                    !ReadDouble(
                        bytes,
                        offset,
                        probability.value)) {
                    return Fail<DecisionResponse>(
                        DecisionCodecStatus::LengthMismatch,
                        "invalid Choice probability");
                }

                value.probabilities.push_back(
                    std::move(probability));
            }

            if (!ReadOptionalConfidence(
                    bytes,
                    offset,
                    value.reportedConfidence)) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::InvalidValue,
                    "invalid Choice confidence");
            }

            answer = std::move(value);
            break;
        }

        case WireAnswerType::Score: {
            ScoreAnswer value;
            std::uint16_t count{};

            if (!ReadDouble(
                    bytes,
                    offset,
                    value.expectedScore) ||
                !ReadIntegral(
                    bytes,
                    offset,
                    count)) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::LengthMismatch,
                    "invalid Score answer");
            }

            if (count > kMaxOptions) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::CountLimit,
                    "Score probability count exceeds limit");
            }

            value.probabilities.reserve(count);

            for (std::uint16_t j = 0; j < count; ++j) {
                double probability{};
                if (!ReadDouble(
                        bytes,
                        offset,
                        probability)) {
                    return Fail<DecisionResponse>(
                        DecisionCodecStatus::LengthMismatch,
                        "invalid Score probability");
                }
                value.probabilities.push_back(
                    probability);
            }

            if (!ReadOptionalConfidence(
                    bytes,
                    offset,
                    value.reportedConfidence)) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::InvalidValue,
                    "invalid Score confidence");
            }

            answer = std::move(value);
            break;
        }

        case WireAnswerType::Abstain: {
            AbstainAnswer value;

            if (!ReadString(
                    bytes,
                    offset,
                    value.reason)) {
                return Fail<DecisionResponse>(
                    DecisionCodecStatus::LengthMismatch,
                    "invalid Abstain answer");
            }

            answer = std::move(value);
            break;
        }

        default:
            return Fail<DecisionResponse>(
                DecisionCodecStatus::InvalidType,
                "unknown decision answer type");
        }

        if (!response.answers.emplace(
                std::move(id),
                std::move(answer)).second) {
            return Fail<DecisionResponse>(
                DecisionCodecStatus::InvalidValue,
                "duplicate answer id");
        }
    }

    if (offset != bytes.size()) {
        return Fail<DecisionResponse>(
            DecisionCodecStatus::LengthMismatch,
            "trailing bytes in decision response");
    }

    CodecResult<DecisionResponse> result;
    result.status = DecisionCodecStatus::Ok;
    result.value = std::move(response);
    return result;
}

} // namespace iphox::cognitive
