#include <paddle/capi.h>
#include <opencv2/opencv.hpp>
#include <time.h>
#include <cstring>
#include <vector>

#include "common/common.h"

#define CONFIG_BIN "./trainer_config.bin"
#define INPUT_SIZE 784

void makeIncrementalData(paddle_matrix& mat)
{
  paddle_real* array;
  int num = 1;
  mat = paddle_matrix_create(/* sample_num */ num,
                                         /* size */INPUT_SIZE,
                                         /* useGPU */ false);
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &array));

  for (int i = 0; i < INPUT_SIZE; ++i) {
    array[i] = i;
  }
}

void makeRandomData(paddle_matrix& mat)
{
  paddle_real* array;
  int num = 1;
  mat = paddle_matrix_create(/* sample_num */ num,
                                         /* size */INPUT_SIZE,
                                         /* useGPU */ false);
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &array));



  for (int i = 0; i < INPUT_SIZE; ++i) {
    array[i] = rand() / ((float)RAND_MAX);
  }
}

void makeFixedData(paddle_matrix& mat, paddle_real value)
{
  paddle_real* array;
  int num = 1;
  mat = paddle_matrix_create(/* sample_num */ num,
                                         /* size */INPUT_SIZE,
                                         /* useGPU */ false);
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &array));

  for (int i = 0; i < INPUT_SIZE; ++i) {
    array[i] = value;
  }
}


/* Wrap the input layer of the network in separate cv::Mat objects
 * (one per channel). This way we save one memcpy 
 * The last preprocessing operation will write the separate channels directly
 * to the input layer. */
// TODO: What is PaddlePaddle data layer format???? (Matrix data format for DataLayer)
// TODO: Finish wrapping around paddle_array

void wrapInputLayerBatch(paddle_matrix& mat, std::vector<std::vector<cv::Mat> >* input_channels_batch, int channels, int height, int width) {

  // Create input matrix.
  int num = 1;
  //mat = paddle_matrix_create([> sample_num <] num,
                                         //[> size <] width*height*channels,
                                         //[> useGPU <] false);

  paddle_real* input_data;
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &input_data));

  for( int j = 0; j < num; ++j) {
      std::vector<cv::Mat> input_channels;
      for (int i = 0; i < channels; ++i) {
          cv::Mat channel(height, width, CV_32FC1, input_data);
          input_channels.push_back(channel);
          input_data += width * height;
      }
      input_channels_batch->push_back(input_channels);
  }
}


void Preprocess(const cv::Mat& img,
                            std::vector<cv::Mat>* input_channels) {
  /* Convert the input image to the input image format of the network. */
  cv::Mat sample;
  int num_channels = 1;
  if (img.channels() == 3 && num_channels == 1)
    cv::cvtColor(img, sample, cv::COLOR_BGR2GRAY);
  else if (img.channels() == 4 && num_channels == 1)
    cv::cvtColor(img, sample, cv::COLOR_BGRA2GRAY);
  else if (img.channels() == 4 && num_channels == 3)
    cv::cvtColor(img, sample, cv::COLOR_BGRA2BGR);
  else if (img.channels() == 1 && num_channels == 3)
    cv::cvtColor(img, sample, cv::COLOR_GRAY2BGR);
  else
    sample = img;


  cv::Size input_geometry_(num_channels, INPUT_SIZE);

  cv::Mat sample_resized;
  if (sample.size() != input_geometry_)
    cv::resize(sample, sample_resized, input_geometry_);
  else
    sample_resized = sample;

  cv::Mat sample_float;
  if (num_channels == 3) {
    sample_resized.convertTo(sample_float, CV_32FC3);
    cv::split(sample_float, *input_channels);
  } else {
    sample_resized = sample_resized.reshape(1,1);
    sample_resized.convertTo((*input_channels)[0], CV_32FC1);
  }
  //cv::Mat sample_normalized;
  //cv::subtract(sample_float, mean_, sample_normalized);

  /* This operation will write the separate BGR planes directly to the
   * input layer of the network because it is wrapped by the cv::Mat
   * objects in input_channels. */
}

void Preprocess(const std::vector<cv::Mat>& imgs,
                            std::vector<std::vector<cv::Mat>>& input_channels_batch) {
  for(size_t i=0; i<input_channels_batch.size(); ++i) {
      Preprocess(imgs[i],&input_channels_batch[i]);
  }
}

paddle_matrix getImageData(char** argv)
{
  cv::Mat image = cv::imread(argv[1], CV_LOAD_IMAGE_COLOR );

  if (image.data == nullptr) {
    printf("ERROR: Image: %s was not read\n",argv[1]);    
    exit(-1);
  }

  std::vector<std::vector<cv::Mat>> obrazki;
  paddle_matrix mat;

  mat = paddle_matrix_create( 1, INPUT_SIZE,  false);
  // Wrap mat data with vector of cv::Mats
  //wrapInputLayerBatch(mat, &obrazki, 3, image.rows, image.cols);
  wrapInputLayerBatch(mat, &obrazki, 1, 1, INPUT_SIZE);

paddle_real* input_data;
CHECK(paddle_matrix_get_row(mat, 0, &input_data));

  Preprocess({image}, obrazki);

CHECK(paddle_matrix_get_row(mat, 0, &input_data));

  cv::Mat tescik = obrazki[0][0];
  printf("sss\n");

  return mat;
}


paddle_matrix prepareData(int mode, char** argv)
{

  // Random data mode
  if(mode == 1) {
    // Create input matrix.
    paddle_matrix mat;
    //makeRandomData(mat);
    makeIncrementalData(mat);
    //makeFixedData(mat,1.0f);
    return mat;
  }

  // Image classification mode
  if(mode == 2) {
    return getImageData(argv);
  }
}


int main(int argc, char** argv) {

  if(argc > 2 ) {
    printf("ERROR: Wrong syntax. Valid syntax:\n \
           test-paddle \n \
           test-paddle <name of image to display> \n \
            ");    
    exit(-1);
  }

  // Initalize Paddle
  char* largv[] = {"--use_mkldnn=True"};
  //char* largv[] = {"--use_gpu=False"};
  CHECK(paddle_init(1, (char**)largv));

  // Reading config binary file. It is generated by `convert_protobin.sh`
  long size;
  void* buf = read_config(CONFIG_BIN, &size);

  // Create a gradient machine for inference.
  paddle_gradient_machine machine;
  CHECK(paddle_gradient_machine_create_for_inference(&machine, buf, (int)size));
  CHECK(paddle_gradient_machine_randomize_param(machine));

  // Loading parameter. Uncomment the following line and change the directory.
  // CHECK(paddle_gradient_machine_load_parameter_from_disk(machine,
  //                                                "./some_where_to_params"));
  paddle_arguments in_args = paddle_arguments_create_none();

  // There is only one input of this network.
  CHECK(paddle_arguments_resize(in_args, 1));

  srand(time(0));

  paddle_matrix mat = prepareData(argc, argv);


  CHECK(paddle_arguments_set_value(in_args, 0, mat));

  paddle_arguments out_args = paddle_arguments_create_none();
  CHECK(paddle_gradient_machine_forward(machine,
                                        in_args,
                                        out_args,
                                        /* isTrain */ false));
  paddle_matrix prob = paddle_matrix_create_none();

  CHECK(paddle_arguments_get_value(out_args, 0, prob));

  uint64_t height;
  uint64_t width;

  paddle_real* array;
  CHECK(paddle_matrix_get_shape(prob, &height, &width));
  CHECK(paddle_matrix_get_row(prob, 0, &array));

 
  printf("Prob: \n");
  for (uint64_t i = 0; i < height * width; ++i) {
    printf("%.4f ", array[i]);
    if ((i + 1) % width == 0) {
      printf("\n");
    }
  }
  printf("\n");

  CHECK(paddle_matrix_destroy(prob));
  CHECK(paddle_arguments_destroy(out_args));
  CHECK(paddle_matrix_destroy(mat));
  CHECK(paddle_arguments_destroy(in_args));
  CHECK(paddle_gradient_machine_destroy(machine));

  return 0;
}