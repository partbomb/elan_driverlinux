#ifndef SIFT_ENGINE_H
#define SIFT_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

int sift_engine_init(void);
int sift_engine_enroll(unsigned char** out_data);
int sift_engine_verify(const unsigned char* saved_data, int data_size);

#ifdef __cplusplus
}
#endif

#endif // SIFT_ENGINE_H