# WwiseMovieRenderOutput
This is an Unreal Engine 5.x plugin for rendering Wwise events in level sequences into a single .wav stem from MRQ.

By default, Wwise audio is not rendered when using MRQ to render level sequences. This simple plugin allows you to create an audio stem (.wav file) of all the Wwise events in the sequence.

# Prerequisites
* Unreal Engine 5
* Movie Render Queue plugin enabled (needed for using MRQ)

# Installation
1. Clone this repository (or download the zip file). 
1. Move the contents to your Unreal projects under `Plugins/WwiseMovieRenderOutput`

# Usage
1. Starup MRQ and edit the config
![MRQ Config](docs/wwisemrq-edit-mrq-config.png)
1. Add the Wwise Movie Render output node
![Add Wwise Movie Render node](docs/wwisemrq-select-output-node.png)
1. Configure the Wwise Movie Render node to override the output filename if desired. By default it will use the naming convention for the first video frame.
![Configure output node](docs/wwisemrq-configure-output.png)

