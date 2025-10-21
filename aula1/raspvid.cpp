
// raspvid_toggle.cpp
// g++ raspvid_toggle.cpp -o raspvid_toggle `pkg-config opencv4 --cflags --libs` -O3 -s
#include <opencv2/opencv.hpp>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdio>

using namespace cv;

static std::string now_filename()
{
    std::time_t t = std::time(nullptr);
    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << "saida_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".avi";
    return oss.str();
}

int main()
{
    VideoCapture cam(0);
    if (!cam.isOpened())
    {
        std::fprintf(stderr, "Erro: não consegui abrir a webcam 0.\n");
        return 1;
    }

    // Tenta 640x480 @ 30 fps
    cam.set(CAP_PROP_FRAME_WIDTH, 640);
    cam.set(CAP_PROP_FRAME_HEIGHT, 480);
    cam.set(CAP_PROP_FPS, 30);

    Mat frame;
    if (!cam.read(frame) || frame.empty())
    {
        std::fprintf(stderr, "Erro: não consegui ler o primeiro quadro.\n");
        return 1;
    }

    namedWindow("janela", WINDOW_AUTOSIZE);

    bool gravando = false;
    VideoWriter vo;                                             // aberto somente quando gravando
    const double fps = 30.0;                                    // ajuste se necessário
    const int fourcc = VideoWriter::fourcc('X', 'V', 'I', 'D'); // ou 'M','J','P','G'
    const Size sz(frame.cols, frame.rows);

    while (true)
    {
        if (!cam.read(frame) || frame.empty())
            break;

        // Se estiver gravando, escreve o frame
        if (gravando && vo.isOpened())
        {
            vo.write(frame);
            // Indica visualmente que está gravando
            putText(frame, "REC", Point(10, 30), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 0, 255), 2);
            circle(frame, Point(70, 20), 8, Scalar(0, 0, 255), FILLED);
        }

        imshow("janela", frame);

        int ch = waitKey(1);
        if (ch == 27 || ch == 'q' || ch == 'Q')
        { // ESC ou Q sai
            break;
        }
        else if (ch == ' ')
        { // ESPACO: alterna gravacao
            if (!gravando)
            {
                std::string fname = now_filename();
                vo.open(fname, fourcc, fps, sz);
                if (!vo.isOpened())
                {
                    std::fprintf(stderr, "Erro: nao consegui abrir o VideoWriter. Tente outro codec (ex.: MJPG).\n");
                }
                else
                {
                    std::printf("Gravando em %s ...\n", fname.c_str());
                    gravando = true;
                }
            }
            else
            {
                // parar
                gravando = false;
                if (vo.isOpened())
                {
                    vo.release();
                    std::printf("Gravacao pausada/parada.\n");
                }
            }
        }
    }

    // limpeza
    if (vo.isOpened())
        vo.release();
    cam.release();
    destroyAllWindows();
    return 0;
}
