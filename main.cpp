#include <paddle/capi.h>
#include <opencv2/opencv.hpp>
#include <time.h>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <streambuf>
#include <sstream>


#include "common/common.h"

int input_width = 0;
int input_height = 0;
int input_num_channels = 0;

void makeIncrementalData(paddle_matrix& mat)
{
  paddle_real* array;
  int num = 1;
  mat = paddle_matrix_create(/* sample_num */ num,
                                         /* size */input_width*input_height*input_num_channels,
                                         /* useGPU */ false);
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &array));

  for (int i = 0; i < input_width*input_height*input_num_channels; ++i) {
    array[i] = i;
  }
}

void makeRandomData(paddle_matrix& mat)
{
  paddle_real* array;
  int num = 1;
  mat = paddle_matrix_create(/* sample_num */ num,
                                         /* size */input_width*input_height*input_num_channels,
                                         /* useGPU */ false);
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &array));

  for (int i = 0; i < input_width*input_height*input_num_channels; ++i) {
    array[i] = rand() / ((float)RAND_MAX);
  }
}

void makeFixedData(paddle_matrix& mat, paddle_real value)
{
  paddle_real* array;
  int num = 1;
  mat = paddle_matrix_create(/* sample_num */ num,
                                         /* size */input_width*input_height*input_num_channels,
                                         /* useGPU */ false);
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &array));

  for (int i = 0; i < input_width*input_height*input_num_channels; ++i) {
    array[i] = value;
  }
}

/* Wrap the input layer of the network in separate cv::Mat objects
 * (one per channel). This way we save one memcpy 
 * The last preprocessing operation will write the separate channels directly
 * to the input layer. */
void wrapInputLayerBatch(paddle_matrix& mat, std::vector<std::vector<cv::Mat> >* input_channels_batch, int channels, int height, int width) {

  // Create input matrix.
  int num = 1;

  paddle_real* input_data;
  // Get First row.
  CHECK(paddle_matrix_get_row(mat, 0, &input_data));

  for( int j = 0; j < num; ++j) {
      std::vector<cv::Mat> input_channels;
      for (int i = 0; i < channels; ++i) {
          cv::Mat channel(height,width, CV_32FC1, input_data);
          input_channels.push_back(channel);
          input_data += width * height;
      }
      input_channels_batch->push_back(input_channels);
  }
}

cv::Mat load_mean_image(std::string mean_file)
{

  std::ifstream ifs;
  ifs.open(mean_file);

  if(ifs.good() == false) {
      std::cout << "Error reading image mean file" << std::endl;
      return cv::Mat();   
  }

  std::vector<cv::Mat> channels;
  for (int c = 0; c < 3; ++c) {
    cv::Mat channel(256, 256, CV_32FC1);
    float parsedfloat;
    int i=0;
    while ( (ifs.eof() == false) && (i<256*256) )
    {
      ifs >> parsedfloat; 
      ((float*)channel.data)[i++] = parsedfloat;
    }
    channels.push_back(channel);
  }

    /* Merge the separate channels into a single image. */
  cv::Mat mean;
  cv::merge(channels, mean);


  /* Compute the global mean pixel value and create a mean image
   * filled with this value. */
  cv::Scalar channel_mean = cv::mean(mean);
  return cv::Mat(cv::Size(input_width, input_height), mean.type(), channel_mean);
}

void Preprocess(const cv::Mat& img,
                            std::vector<cv::Mat>* input_channels) {
  /* Convert the input image to the input image format of the network. */
  cv::Mat sample;
  if (img.channels() == 3 && input_num_channels == 1)
    cv::cvtColor(img, sample, cv::COLOR_BGR2GRAY);
  else if (img.channels() == 4 && input_num_channels == 1)
    cv::cvtColor(img, sample, cv::COLOR_BGRA2GRAY);
  else if (img.channels() == 4 && input_num_channels == 3)
    cv::cvtColor(img, sample, cv::COLOR_BGRA2BGR);
  else if (img.channels() == 1 && input_num_channels == 3)
    cv::cvtColor(img, sample, cv::COLOR_GRAY2BGR);
  else
    sample = img;

  cv::Size input_geometry_(input_width, input_height);

  cv::Mat sample_resized;
  if (sample.size() != input_geometry_)
    cv::resize(sample, sample_resized, input_geometry_);
  else
    sample_resized = sample;

  cv::Mat sample_float;
  cv::Mat sample_normalized;
  if (input_num_channels == 3) {
    sample_resized.convertTo(sample_float, CV_32FC3);
    // Load image mean file
    cv::Mat mean, mean_resized;
    mean = load_mean_image("imagenet.mean");
    cv::resize(mean, mean_resized, input_geometry_); 
    cv::subtract(sample_float, mean_resized, sample_normalized);
    cv::split(sample_normalized, *input_channels);
  } else {
    sample_resized = sample_resized.reshape(1,1);
    sample_resized.convertTo((*input_channels)[0], CV_32FC1);
  }

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

  mat = paddle_matrix_create( 1, input_width*input_height*input_num_channels,  false);
  // Wrap mat data with vector of cv::Mats
  wrapInputLayerBatch(mat, &obrazki, input_num_channels, input_height, input_width);

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


// Read lines of file with names of categories and print
void printCategoryName(uint64_t idx_to_print, float prob)  
{
  std::ifstream ifs;
  ifs.open("./synset_words.txt");

  // If there is no /proc/cpuinfo fallback to default scheduler
  if(ifs.good() == false) {
      std::cout << "Error reading synset_words.txt file" << std::endl;
      return;   
  }
  std::string cpuinfo_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  std::stringstream cpuinfo_stream(cpuinfo_content);
  std::string category_line;
  std::string cpu_name;
  int curr_idx = 0;
  while(std::getline(cpuinfo_stream, category_line,'\n')){
    if (curr_idx == idx_to_print) {
      std::cout << "Top1: " << prob <<" "<< category_line << std::endl; 
    }
    ++curr_idx;
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
  std::string config_bin("");
  if(argc == 1 ) {
    config_bin = "./trainer_config.bin";
  } else {
    config_bin = "./trainer_config_alexnet.bin";
  }

  long size;
  void* buf = read_config(config_bin.c_str(), &size);

  // Create a gradient machine for inference.
  paddle_gradient_machine machine;
  CHECK(paddle_gradient_machine_create_for_inference(&machine, buf, (int)size));
  CHECK(paddle_gradient_machine_randomize_param(machine));

  // Loading parameter. Uncomment the following line and change the directory.
  if (argc == 2) {
    CHECK(paddle_gradient_machine_load_parameter_from_disk(machine, "./Paddle_bvlc_alexnet"));
    input_width = 227;
    input_height = 227;
    input_num_channels = 3;
  } else {
    input_width = 28;
    input_height = 28;
    input_num_channels = 1;
  }

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
  uint64_t argmax_i = 0;
  float max_prob = 0.0;
  for (uint64_t i = 0; i < height * width; ++i) {
    printf("%.4f ", array[i]);
    if ((i + 1) % width == 0) {
      printf("\n");
    }
    if(array[i] > max_prob) {
      max_prob = array[i];
      argmax_i = i;
    }
  }
  printf("\n");

  // For alexnet , print categories
  if (argc == 2) {
    printCategoryName(argmax_i, max_prob);  
  }


  CHECK(paddle_matrix_destroy(prob));
  CHECK(paddle_arguments_destroy(out_args));
  CHECK(paddle_matrix_destroy(mat));
  CHECK(paddle_arguments_destroy(in_args));
  CHECK(paddle_gradient_machine_destroy(machine));

  return 0;
}
