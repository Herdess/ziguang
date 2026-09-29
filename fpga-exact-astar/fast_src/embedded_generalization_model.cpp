#include "generalization_model.hpp"

namespace srb {

#if defined(SRB_EMBED_GENERALIZATION_MODEL)
extern "C" {
extern const unsigned char _binary_generalization_model_srb_start[];
extern const unsigned char _binary_generalization_model_srb_end[];
}
#endif

EmbeddedGeneralizationModel embedded_generalization_model() {
#if defined(SRB_EMBED_GENERALIZATION_MODEL)
    return {
        _binary_generalization_model_srb_start,
        static_cast<size_t>(_binary_generalization_model_srb_end -
                            _binary_generalization_model_srb_start),
    };
#else
    return {};
#endif
}

}  // namespace srb
