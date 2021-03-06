/*
  libdeep - a library for deep learning
  Copyright (C) 2016  Bob Mottram <bob@freedombone.net>

  Differentiable Neural Computer (DNC)
  A neural Turing Machine architecture based on the paper:

  "Hybrid computing using a neural network with dynamic external memory"
  Nature, 2016

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
  3. Neither the name of the University nor the names of its contributors
  may be used to endorse or promote products derived from this software
  without specific prior written permission.
  .
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE HOLDERS OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "deeplearn_dnc.h"

/**
 * @brief Allocates memory for the memory of the neural computer
 * @param learner The DNC object
 * @param memory_size Number of addresses within the memory space
 * @param memory_width The array/vector size of each memory address
 * @returns zero on success
 */
static int deeplearn_dnc_init_memory(deeplearn_dnc * learner, int memory_size,
                                     int memory_width)
{
    int i;

    learner->memory.size =
        (unsigned int)((memory_size/
                       DEEPLEARNDNC_USAGE_BLOCK_SIZE)*
                       DEEPLEARNDNC_USAGE_BLOCK_SIZE);
    learner->memory.width = (unsigned int)memory_width;
    learner->memory.address =
        (float**)malloc(learner->memory.size * sizeof(float*));
    if (learner->memory.address == NULL) return 1;

    /* allocate memory vectors */
    for (i = 0; i < learner->memory.size; i++) {
        learner->memory.address[i] =
            (float*)malloc(learner->memory.width * sizeof(float));
        if (learner->memory.address[i] == NULL) return 2;
    }
    return 0;
}

/**
 * @brief Deallocates the memory of the neural computer
 * @param learner The DNC object
 */
static void deeplearn_dnc_free_memory(deeplearn_dnc * learner)
{
    int i;

    for (i = 0; i < learner->memory.size; i++) {
        free(learner->memory.address[i]);
    }
    free(learner->memory.address);
}

/**
 * @brief Allocates the memory usage and temporal matrix of the neural computer
 * @param learner The DNC object
 * @returns zero on success
 */
static int deeplearn_dnc_init_memory_usage(deeplearn_dnc * learner)
{
    /* The memory usage array is downsampled by the block size */
    int i, memory_usage_size = learner->memory.size / DEEPLEARNDNC_USAGE_BLOCK_SIZE;

    /* memory usage */
    learner->memory.usage =
        (float*)malloc(memory_usage_size * sizeof(float));
    if (learner->memory.usage == NULL) return 1;

    /* temporal matrix of memory usage. This encodes which address was used
       after which previous address for each read and write head */
    for (i = 0; i < DEEPLEARNDNC_READ_HEADS + DEEPLEARNDNC_WRITE_HEADS; i++) {
        learner->memory.usage_temporal[i] =
            (float*)malloc(memory_usage_size * memory_usage_size * sizeof(float));
        if (learner->memory.usage_temporal[i] == NULL) return 2;
    }

    return 0;
}

/**
 * @brief Deallocates memory usage and temporal matrix of the neural computer
 * @param learner The DNC object
 */
static void deeplearn_dnc_free_memory_usage(deeplearn_dnc * learner)
{
    int i;

    free(learner->memory.usage);
    for (i = 0; i < DEEPLEARNDNC_READ_HEADS + DEEPLEARNDNC_WRITE_HEADS; i++)
        free(learner->memory.usage_temporal[i]);
}

/**
 * @brief Allocates memory for the read and write heads of the neural computer
 * @param learner The DNC object
 * @returns Zero on succes
 */
static int deeplearn_dnc_init_heads(deeplearn_dnc * learner)
{
    int i;

    for (i = 0; i < DEEPLEARNDNC_READ_HEADS; i++) {
        learner->read_head[i].key = (float*)malloc(learner->memory.width * sizeof(float));
        if (learner->read_head[i].key == NULL) return 1;
    }

    for (i = 0; i < DEEPLEARNDNC_WRITE_HEADS; i++) {
        learner->write_head[i].write = (float*)malloc(learner->memory.width * sizeof(float));
        if (learner->write_head[i].write == NULL) return 2;
        learner->write_head[i].erase = (float*)malloc(learner->memory.width * sizeof(float));
        if (learner->write_head[i].erase == NULL) return 3;
        learner->write_head[i].key = (float*)malloc(learner->memory.width * sizeof(float));
        if (learner->write_head[i].key == NULL) return 4;
    }

    return 0;
}

/**
 * @brief Deallocates memory for the read and write heads of the neural computer
 * @param learner The DNC object
 */
static void deeplearn_dnc_free_heads(deeplearn_dnc * learner)
{
    int i;

    for (i = 0; i < DEEPLEARNDNC_READ_HEADS; i++) {
        free(learner->read_head[i].key);
    }

    for (i = 0; i < DEEPLEARNDNC_WRITE_HEADS; i++) {
        free(learner->write_head[i].write);
        free(learner->write_head[i].erase);
        free(learner->write_head[i].key);
    }
}

/**
 * @brief Initialises the neural computer
 * @param learner The DNC object to be initialised
 * @param memory_size The number of addresses within the memory space
 * @param memory_width The array/vector size for each memory address
 * @param no_of_inputs
 * @param no_of_hiddens The number of hidden units on the first layer
 * @param hidden_layers The number of hidden layers
 * @param no_of_outputs The number of outputs
 * @param error_threshold Maximum error thresholds for training each layer
 * @param random_seed Random number generator seed
 * @returns zero on success
 */
int deeplearn_dnc_init(deeplearn_dnc * learner,
                       int memory_size, int memory_width,
                       int no_of_inputs,
                       int no_of_hiddens,
                       int hidden_layers,
                       int no_of_outputs,
                       float error_threshold[],
                       unsigned int * random_seed)
{
    unsigned int retval;

    int controller_inputs =
        no_of_inputs +
        (memory_width*DEEPLEARNDNC_READ_HEADS);

    int controller_outputs =
        no_of_outputs +
        (memory_width*DEEPLEARNDNC_WRITE_HEADS) +
        ((memory_width+3)*DEEPLEARNDNC_READ_HEADS);

    learner->no_of_inputs = no_of_inputs;
    learner->no_of_outputs = no_of_outputs;

    learner->controller = (deeplearn*)malloc(sizeof(deeplearn));
    if (learner->controller==NULL) return 1000;

    retval = deeplearn_dnc_init_memory(learner, memory_size, memory_width);
    if (retval != 0) return 2000 + retval;

    retval = deeplearn_dnc_init_memory_usage(learner);
    if (retval != 0) return 3000 + retval;

    retval = deeplearn_dnc_init_heads(learner);
    if (retval != 0) return 4000 + retval;

    retval = deeplearn_init(learner->controller,
                            controller_inputs,
                            no_of_hiddens,
                            hidden_layers,
                            controller_outputs,
                            error_threshold,
                            random_seed);
    if (retval != 0) return 5000 + retval;

    return 0;
}

/**
 * @brief Performs an update of the neural network without learning
 * @param learner The DNC object
 */
void deeplearn_dnc_feed_forward(deeplearn_dnc * learner)
{
    deeplearn_dnc_update_read_heads(learner);
    deeplearn_feed_forward(learner->controller);
}

/**
 * @brief Performs an update of the neural network with learning
 * @param learner The DNC object
 */
void deeplearn_dnc_update(deeplearn_dnc * learner)
{
    deeplearn_update(learner->controller);
}

/**
 * @brief Deallocates memory for the given neural computer
 * @param learner The DNC object
 */
void deeplearn_dnc_free(deeplearn_dnc * learner)
{
    deeplearn_dnc_free_memory(learner);
    deeplearn_dnc_free_memory_usage(learner);
    deeplearn_dnc_free_heads(learner);

    /* free controller */
    deeplearn_free(learner->controller);
}

/**
 * @brief Set inputs from text
 * @param learner The DNC object
 * @param text Text string
 */
void deeplearn_dnc_set_input_text(deeplearn_dnc * learner, char * text)
{
    deeplearn_set_input_text(learner->controller, text);
}

/**
 * @brief Sets an input value
 * @param learner The DNC object
 * @param index Index of the input
 * @param value Value of the input
 */
void deeplearn_dnc_set_input(deeplearn_dnc * learner, int index, float value)
{
    deeplearn_set_input(learner->controller, index, value);
}

/**
 * @brief Sets a numeric value for the given input field
 * @param learner DNC object
 * @param fieldindex Index number of the input field.
 *        This is not necessarily the same as the input index
 * @param value Value to set the input unit to in the range 0.0 to 1.0
 * @returns zero on success
 */
int deeplearn_dnc_set_input_field(deeplearn_dnc * learner, int fieldindex, float value)
{
    return deeplearn_set_input_field(learner->controller, fieldindex, value);
}

/**
 * @brief Sets a text value for the given input field
 * @param learner DNC object
 * @param fieldindex Index number of the input field.
 *        This is not necessarily the same as the input index
 * @param text Text value for the field
 * @returns zero on success
 */
int deeplearn_dnc_set_input_field_text(deeplearn_dnc * learner, int fieldindex, char * text)
{
    return deeplearn_set_input_field_text(learner->controller, fieldindex, text);
}

/**
 * @brief Sets inputs from the given data sample.
 *        The sample can contain arbitrary floating point values, so these
 *        need to be normalised into a 0.25-0.75 range
 * @param learner DNC object
 */
void deeplearn_dnc_set_inputs(deeplearn_dnc * learner, deeplearndata * sample)
{
    deeplearn_set_inputs(learner->controller, sample);
}

/**
 * @brief Sets the value of an output unit
 * @param learner DNC object
 * @param index Index number of the output unit
 * @param value Value to set the output unit to in the range 0.0 to 1.0
 */
void deeplearn_dnc_set_output(deeplearn_dnc * learner, int index, float value)
{
    deeplearn_set_output(learner->controller, index, value);
}

/**
 * @brief Sets outputs from the given data sample.
 *        The sample can contain arbitrary floating point values, so these
 *        need to be normalised into a 0.25-0.75 range
 * @param learner DNC object
 * @param sample The data sample from which to obtain the output values
 */
void deeplearn_dnc_set_outputs(deeplearn_dnc * learner, deeplearndata * sample)
{
    deeplearn_set_outputs(learner->controller, sample);
}

/**
 * @brief Returns the values of outputs within their normal range
 * @param learner DNC object
 * @param outputs The returned output values
 */
void deeplearn_dnc_get_outputs(deeplearn_dnc * learner, float * outputs)
{
    deeplearn_get_outputs(learner->controller, outputs);
}

/**
 * @brief Returns the value of an output unit
 * @param learner DNC object
 * @param index Index number of the output unit
 * @return Value of the output unit to in the range 0.0 to 1.0
 */
float deeplearn_dnc_get_output(deeplearn_dnc * learner, int index)
{
    return deeplearn_get_output(learner->controller, index);
}

/**
 * @brief Gets the output class as an integer value
 * @param learner DNC object
 * @return output class
 */
int deeplearn_dnc_get_class(deeplearn_dnc * learner)
{
    return deeplearn_get_class(learner->controller);
}

/**
 * @brief Sets the output class
 * @param learner DNC object
 * @param class The class number
 */
void deeplearn_dnc_set_class(deeplearn_dnc * learner, int class)
{
    deeplearn_set_class(learner->controller, class);
}

/**
 * @brief Saves the given DNC object to a file
 * @param fp File pointer
 * @param learner DNC object
 * @return zero value on success
 */
int deeplearn_dnc_save(FILE * fp, deeplearn_dnc * learner)
{
    return deeplearn_save(fp, learner->controller);
}

/**
 * @brief Loads a DNC object from file
 * @param fp File pointer
 * @param learner DNC object
 * @param random_seed Random number generator seed
 * @return zero value on success
 */
int deeplearn_dnc_load(FILE * fp, deeplearn_dnc * learner,
                       unsigned int * random_seed)
{
    return deeplearn_load(fp, learner->controller, random_seed);
}

/**
 * @brief Compares two DNCs and returns a greater
 *        than zero value if they are the same
 * @param learner1 First DNC object
 * @param learner2 Second DNC object
 * @return Greater than zero if the two learners are the same
 */
int deeplearn_dnc_compare(deeplearn_dnc * learner1,
                          deeplearn_dnc * learner2)
{
    return deeplearn_compare(learner1->controller,
                             learner2->controller);
}

/**
 * @brief Uses gnuplot to plot the training error for the given learner
 * @param learner DNC object
 * @param filename Filename for the image to save as
 * @param title Title of the graph
 * @param image_width Width of the image in pixels
 * @param image_height Height of the image in pixels
 * @return zero on success
 */
int deeplearn_dnc_plot_history(deeplearn_dnc * learner,
                               char * filename, char * title,
                               int image_width, int image_height)
{
    return deeplearn_plot_history(learner->controller,
                                  filename, title, image_width, image_height);
}

/**
 * @brief Updates the input units from a patch within a larger image
 * @param learner DNC object
 * @param img Image buffer (1 byte per pixel)
 * @param image_width Width of the image in pixels
 * @param image_height Height of the image in pixels
 * @param tx Top left x coordinate of the patch
 * @param ty Top left y coordinate of the patch
 */
void deeplearn_dnc_inputs_from_image_patch(deeplearn_dnc * learner,
                                           unsigned char * img,
                                           int image_width, int image_height,
                                           int tx, int ty)
{
    deeplearn_inputs_from_image_patch(learner->controller,
                                      img, image_width, image_height,
                                      tx, ty);
}

/**
 * @brief Updates the input units from an image
 * @param learner DNC object
 * @param img Image buffer (1 byte per pixel)
 * @param image_width Width of the image in pixels
 * @param image_height Height of the image in pixels
 */
void deeplearn_dnc_inputs_from_image(deeplearn_dnc * learner,
                                     unsigned char * img,
                                     int image_width, int image_height)
{
    deeplearn_inputs_from_image(learner->controller,
                                img, image_width, image_height);
}

/**
 * @brief Sets the learning rate
 * @param learner DNC object
 * @param rate the learning rate in the range 0.0 to 1.0
 */
void deeplearn_dnc_set_learning_rate(deeplearn_dnc * learner, float rate)
{
    deeplearn_set_learning_rate(learner->controller, rate);
}

/**
 * @brief Sets the percentage of units which drop out during training
 * @param learner DNC object
 * @param dropout_percent Percentage of units which drop out in the range 0 to 100
 */
void deeplearn_dnc_set_dropouts(deeplearn_dnc * learner, float dropout_percent)
{
    deeplearn_set_dropouts(learner->controller, dropout_percent);
}

/**
 * @brief Exports a trained network as a standalone program
 *        file types supported are .c and .py
 * @param learner DNC object
 * @param filename The source file to be produced
 * @returns zero on success
 */
int deeplearn_dnc_export(deeplearn_dnc * learner, char * filename)
{
    return deeplearn_export(learner->controller, filename);
}

/**
 * @brief Returns a training error threshold for the given layer
 * @param learner DNC object
 * @param index Layer index
 * @returns Training error threshold (percentage value)
 */
float deeplearn_dnc_get_error_threshold(deeplearn_dnc * learner, int index)
{
    return deeplearn_get_error_threshold(learner->controller, index);
}

/**
 * @brief Sets a training error threshold
 * @param learner DNC object
 * @param index Layer index
 * @param value Threshold value as a percentage
 */
void deeplearn_dnc_set_error_threshold(deeplearn_dnc * learner, int index, float value)
{
    deeplearn_set_error_threshold(learner->controller, index, value);
}

/**
 * @brief Perform continuous unsupervised learning
 * @param learner DNC object
 */
void deeplearn_dnc_update_continuous(deeplearn_dnc * learner)
{
    deeplearn_update_continuous(learner->controller);
}

/**
 * @brief Returns true if currently training the final layer
 * @param learner DNC object
 * @returns True if training the last layer
 */
int deeplearn_dnc_training_last_layer(deeplearn_dnc * learner)
{
    return deeplearn_training_last_layer(learner->controller);
}

/**
 * @brief Clears the memory of the neural computer
 * @param learner DNC object
 */
void deeplearn_dnc_clear_memory(deeplearn_dnc * learner)
{
    int i, memory_usage_size = learner->memory.size / DEEPLEARNDNC_USAGE_BLOCK_SIZE;

    /* clear all address vectors */
    for (i = 0; i < learner->memory.size; i++)
        memset((void*)learner->memory.address[i], '\0',
               learner->memory.width * sizeof(float));

    /* clear the memory usage */
    memset((void*)learner->memory.usage, '\0',
           memory_usage_size * sizeof(float*));

    /* clear the temporal matrix */
    memset((void*)learner->memory.usage_temporal, '\0',
           memory_usage_size * memory_usage_size * sizeof(float*));
}

/**
 * @brief Updates the read heads of the neural computer
 * @param learner DNC object
 */
void deeplearn_dnc_update_read_heads(deeplearn_dnc * learner)
{
}

/**
 * @brief Updates the write heads of the neural computer
 * @param learner DNC object
 */
void deeplearn_dnc_update_write_heads(deeplearn_dnc * learner)
{
}
