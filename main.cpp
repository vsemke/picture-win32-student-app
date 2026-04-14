/* NAME: Andrey Anatolievich Melnikov, ИТз-441
 * ASGN: N1
 */

#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commdlg.h>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include "resource.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(linker, "/subsystem:windows")

// --- Lang.ini поддержка: UTF-8 → UTF-16 кэш в ОЗУ ---
static std::unordered_map<std::wstring, std::wstring> g_lang_cache;
static bool g_lang_loaded = false;

// Конвертирует UTF-8 строку в UTF-16 (wchar_t)
static std::wstring UTF8ToWide(const char* utf8_str)
{
    if (!utf8_str) return std::wstring();

    int len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, nullptr, 0);
    if (len <= 0) return std::wstring();

    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, &result[0], len);
    return result;
}

// Загружает все строки из lang.ini (файл в UTF-8) → кэширует в ОЗУ как CP1251 wide-строки
static void LoadLangStrings()
{
    if (g_lang_loaded) return;

    // Путь к lang.ini рядом с exe
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    wchar_t* last_backslash = wcsrchr(exe_path, L'\\');
    if (last_backslash)
        wcscpy(last_backslash + 1, L"lang.ini");
    else
        wcscpy(exe_path, L"lang.ini");

    HANDLE hfile = CreateFileW(exe_path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile == INVALID_HANDLE_VALUE)
    {
        g_lang_loaded = true;
        return;
    }

    DWORD file_size = GetFileSize(hfile, NULL);
    if (file_size == 0 || file_size == INVALID_FILE_SIZE)
    {
        CloseHandle(hfile);
        g_lang_loaded = true;
        return;
    }

    char* buffer = new (std::nothrow) char[file_size + 1];
    if (!buffer)
    {
        CloseHandle(hfile);
        g_lang_loaded = true;
        return;
    }

    DWORD bytes_read;
    if (!ReadFile(hfile, buffer, file_size, &bytes_read, NULL) || bytes_read != file_size)
    {
        delete[] buffer;
        CloseHandle(hfile);
        g_lang_loaded = true;
        return;
    }
    buffer[bytes_read] = '\0';
    CloseHandle(hfile);

    // Парсим INI: секция [PictureStrings], ключ=значение
    bool in_section = false;
    char* pos = buffer;

    while (*pos)
    {
        // Пропуск комментариев и пустых строк
        if (*pos == ';' || *pos == '\n' || *pos == '\r')
        {
            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }

        // Проверка секции
        if (*pos == '[')
        {
            in_section = (strncmp(pos, "[PictureStrings]", 16) == 0);
            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }

        // Парсинг ключ=значение в нужной секции
        if (in_section)
        {
            char* eq = strchr(pos, '=');
            if (eq)
            {
                // Длина ключа (до '=' без пробелов)
                int key_len = 0;
                while (key_len < static_cast<int>(eq - pos) && pos[key_len] != ' ' && pos[key_len] != '\t')
                    key_len++;

                // Значение — от '=' до конца строки
                char* val_start = eq + 1;
                char* val_end = val_start;
                while (*val_end && *val_end != '\n' && *val_end != '\r') val_end++;

                // Ключ: UTF-8 → UTF-16 wide
                char key_tmp[256];
                if (key_len < 256)
                {
                    memcpy(key_tmp, pos, key_len);
                    key_tmp[key_len] = '\0';
                    std::wstring key_w = UTF8ToWide(key_tmp);

                    // Значение: собираем UTF-8 строку, обрабатываем escape, конвертируем в UTF-16 wide
                    std::string val_utf8(val_start, static_cast<size_t>(val_end - val_start));
                    std::string val_processed;
                    for (size_t i = 0; i < val_utf8.size(); i++)
                    {
                        if (val_utf8[i] == '\\' && (i + 1) < val_utf8.size())
                        {
                            if (val_utf8[i + 1] == 'n')
                            {
                                val_processed += '\n';
                                i++;
                            }
                            else if (val_utf8[i + 1] == '\\')
                            {
                                val_processed += '\\';
                                i++;
                            }
                            else
                            {
                                val_processed += val_utf8[i];
                            }
                        }
                        else
                        {
                            val_processed += val_utf8[i];
                        }
                    }
                    std::wstring val_w = UTF8ToWide(val_processed.c_str());

                    if (!key_w.empty())
                        g_lang_cache[key_w] = val_w;
                }
            }

            while (*pos && *pos != '\n') pos++;
            if (*pos == '\n') pos++;
            continue;
        }

        while (*pos && *pos != '\n') pos++;
        if (*pos == '\n') pos++;
    }

    delete[] buffer;
    g_lang_loaded = true;
}

// Получить UTF-16 wide-строку из кэша по ключу
static const wchar_t* LangStr(const wchar_t* key)
{
    LoadLangStrings();
    auto it = g_lang_cache.find(key);
    if (it != g_lang_cache.end())
    {
        return it->second.c_str();
    }
    return key; // fallback: ключ если не найдено
}

// --- Создание меню программно из lang.ini ---
static HMENU CreateAppMenu()
{
    HMENU hMenu = CreateMenu();
    if (!hMenu) return NULL;

    // Popup 1: Файл
    HMENU hPopupFile = CreatePopupMenu();
    if (hPopupFile)
    {
        AppendMenuW(hPopupFile, MF_STRING, ID_FILE_SAVE, LangStr(L"IDS_FILE_SAVE"));
        AppendMenuW(hPopupFile, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hPopupFile, MF_STRING, ID_FILE_EXIT, LangStr(L"IDS_FILE_EXIT"));
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPopupFile, LangStr(L"IDS_MENU_FILE"));
    }

    // Popup 2: Новое задание
    HMENU hPopupNewTask = CreatePopupMenu();
    if (hPopupNewTask)
    {
        AppendMenuW(hPopupNewTask, MF_STRING, ID_NEW_TASK, LangStr(L"IDS_NEW_TASK"));
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPopupNewTask, LangStr(L"IDS_MENU_NEW_TASK"));
    }

    // Popup 3: О программе
    HMENU hPopupAbout = CreatePopupMenu();
    if (hPopupAbout)
    {
        AppendMenuW(hPopupAbout, MF_STRING, ID_ABOUT, LangStr(L"IDS_ABOUT"));
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPopupAbout, LangStr(L"IDS_MENU_ABOUT"));
    }

    return hMenu;
}

// Массив из 3 точек, отмеченных пользователем
POINT g_points[3];

// Количество отмеченных точек (0, 1, 2 или 3)
int g_point_count = 0;

// Флаг: парабола вычислена и готова к отрисовке
bool g_parabola_ready = false;

// Коэффициенты параболы y = ax² + bx + c
double g_coeff_a = 0.0;
double g_coeff_b = 0.0;
double g_coeff_c = 0.0;

const int ARM_LENGTH = 10;

void DrawCrosshair(HDC hdc, POINT pt)
{
    MoveToEx(hdc, pt.x - ARM_LENGTH, pt.y, NULL);
    LineTo(hdc, pt.x + ARM_LENGTH, pt.y);
    MoveToEx(hdc, pt.x, pt.y - ARM_LENGTH, NULL);
    LineTo(hdc, pt.x, pt.y + ARM_LENGTH);
}

bool SolveParabola(POINT pts[3], double &a, double &b, double &c)
{
    // Матрица [A|B] размером 3x4
    double m[3][4];
    for (int i = 0; i < 3; i++)
    {
        double x = static_cast<double>(pts[i].x);
        double y = static_cast<double>(pts[i].y);
        m[i][0] = x * x;
        m[i][1] = x;
        m[i][2] = 1.0;
        m[i][3] = y;
    }

    // Прямой ход Гаусса
    for (int col = 0; col < 3; col++)
    {
        // Поиск ведущего элемента
        int pivot = col;
        double max_val = fabs(m[col][col]);
        for (int row = col + 1; row < 3; row++)
        {
            double current = fabs(m[row][col]);
            if (current > max_val)
            {
                max_val = current;
                pivot = row;
            }
        }

        // Проверка на вырожденность системы
        if (max_val < 1e-10)
        {
            return false;
        }

        // Перестановка строк
        if (pivot != col)
        {
            for (int j = 0; j < 4; j++)
            {
                double tmp = m[col][j];
                m[col][j] = m[pivot][j];
                m[pivot][j] = tmp;
            }
        }

        // Нормализация ведущей строки
        double div = m[col][col];
        for (int j = col; j < 4; j++)
        {
            m[col][j] /= div;
        }

        // Обнуление столбца ниже и выше
        for (int row = 0; row < 3; row++)
        {
            if (row != col && fabs(m[row][col]) > 1e-10)
            {
                double factor = m[row][col];
                for (int j = col; j < 4; j++)
                {
                    m[row][j] -= factor * m[col][j];
                }
            }
        }
    }

    // Обратный ход - теперь матрица приведена к единичной
    a = m[0][3];
    b = m[1][3];
    c = m[2][3];

    return true;
}

void DrawParabola(HDC hdc, HWND hwnd, double a, double b, double c)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    HPEN hpen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
    if (!hpen) return;

    HPEN hpen_old = static_cast<HPEN>(SelectObject(hdc, hpen));

    // Собираем точки в сегменты (непрерывные участки внутри клиентской области)
    int width = rc.right - rc.left + 1;
    POINT* points = new (std::nothrow) POINT[width];
    if (!points)
    {
        SelectObject(hdc, hpen_old);
        DeleteObject(hpen);
        return;
    }

    int seg_start = 0;
    int seg_count = 0;

    for (int x = rc.left; x <= rc.right; x++)
    {
        double xd = static_cast<double>(x);
        double y = a * xd * xd + b * xd + c;
        int yi = static_cast<int>(round(y));

        if (yi >= rc.top && yi <= rc.bottom)
        {
            points[seg_count].x = x;
            points[seg_count].y = yi;
            seg_count++;
        }
        else
        {
            // Рисуем накопленный сегмент
            if (seg_count - seg_start > 1)
            {
                Polyline(hdc, points + seg_start, seg_count - seg_start);
            }
            seg_start = seg_count;
        }
    }

    // Рисуем последний сегмент
    if (seg_count - seg_start > 1)
    {
        Polyline(hdc, points + seg_start, seg_count - seg_start);
    }

    delete[] points;
    SelectObject(hdc, hpen_old);
    DeleteObject(hpen);
}

void SaveBmpDialog(HWND hwnd)
{
    wchar_t file_path[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Bitmap files (*.bmp)\0*.bmp\0All files\0*.*\0";
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"bmp";
    ofn.lpstrTitle = LangStr(L"IDS_SAVE_DIALOG_TITLE");
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;

    // Получаем размер клиентской области
    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    if (width <= 0 || height <= 0)
    {
        MessageBoxW(hwnd, LangStr(L"IDS_INVALID_WINDOW_SIZE"), LangStr(L"IDS_ERROR_TITLE"), MB_ICONERROR);
        return;
    }

    // Создаём совместимый DC и bitmap
    HDC hdc_window = GetDC(hwnd);
    if (!hdc_window) return;

    HDC hdc_mem = CreateCompatibleDC(hdc_window);
    if (!hdc_mem)
    {
        ReleaseDC(hwnd, hdc_window);
        return;
    }

    HBITMAP hbmp = CreateCompatibleBitmap(hdc_window, width, height);
    if (!hbmp)
    {
        DeleteDC(hdc_mem);
        ReleaseDC(hwnd, hdc_window);
        return;
    }

    HBITMAP hbmp_old = static_cast<HBITMAP>(SelectObject(hdc_mem, hbmp));

    // Заливаем фон белым перед копированием
    RECT rc_mem = {0, 0, width, height};
    FillRect(hdc_mem, &rc_mem, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    // Копируем содержимое окна в memory DC
    BitBlt(hdc_mem, 0, 0, width, height, hdc_window, 0, 0, SRCCOPY);

    // Подготавливаем BITMAPINFO
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height; // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    // Размер строки с выравниванием на 4 байта
    int row_size = ((width * 3 + 3) / 4) * 4;
    int image_size = row_size * height;

    BYTE *pixels = new (std::nothrow) BYTE[image_size];
    if (!pixels)
    {
        SelectObject(hdc_mem, hbmp_old);
        DeleteObject(hbmp);
        DeleteDC(hdc_mem);
        ReleaseDC(hwnd, hdc_window);
        MessageBoxW(hwnd, LangStr(L"IDS_OUT_OF_MEMORY"), LangStr(L"IDS_ERROR_TITLE"), MB_ICONERROR);
        return;
    }

    int dib_result = GetDIBits(hdc_mem, hbmp, 0, height, pixels,
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    if (dib_result == 0)
    {
        delete[] pixels;
        SelectObject(hdc_mem, hbmp_old);
        DeleteObject(hbmp);
        DeleteDC(hdc_mem);
        ReleaseDC(hwnd, hdc_window);
        MessageBoxW(hwnd, LangStr(L"IDS_DIB_ERROR"), LangStr(L"IDS_ERROR_TITLE"), MB_ICONERROR);
        return;
    }

    // Записываем файл
    BITMAPFILEHEADER bf = {};
    bf.bfType = 0x4D42; // 'BM'
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfSize = bf.bfOffBits + image_size;

    HANDLE hfile = CreateFileW(file_path, GENERIC_WRITE, 0,
                               NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        BOOL ok = TRUE;
        ok = ok && WriteFile(hfile, &bf, sizeof(bf), &written, NULL);
        ok = ok && WriteFile(hfile, &bi, sizeof(bi), &written, NULL);
        ok = ok && WriteFile(hfile, pixels, image_size, &written, NULL);
        CloseHandle(hfile);
        if (ok)
        {
            MessageBoxW(hwnd, LangStr(L"IDS_FILE_SAVED"), LangStr(L"IDS_SAVE_SUCCESS_TITLE"), MB_OK);
        }
        else
        {
            MessageBoxW(hwnd, LangStr(L"IDS_FILE_WRITE_ERROR"), LangStr(L"IDS_ERROR_TITLE"), MB_ICONERROR);
        }
    }
    else
    {
        MessageBoxW(hwnd, LangStr(L"IDS_FILE_SAVE_ERROR"), LangStr(L"IDS_ERROR_TITLE"), MB_ICONERROR);
    }

    delete[] pixels;
    SelectObject(hdc_mem, hbmp_old);
    DeleteObject(hbmp);
    DeleteDC(hdc_mem);
    ReleaseDC(hwnd, hdc_window);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            // Создаём меню программно из lang.ini
            HMENU hMenu = CreateAppMenu();
            if (hMenu)
            {
                SetMenu(hwnd, hMenu);
            }
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Рисуем крестики для всех отмеченных точек
            HPEN hpen_cross = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            if (hpen_cross)
            {
                HPEN hpen_old = static_cast<HPEN>(SelectObject(hdc, hpen_cross));

                for (int i = 0; i < g_point_count; i++)
                {
                    DrawCrosshair(hdc, g_points[i]);
                }

                SelectObject(hdc, hpen_old);
                DeleteObject(hpen_cross);
            }

            // Рисуем параболу если все 3 точки отмечены
            if (g_parabola_ready)
            {
                DrawParabola(hdc, hwnd, g_coeff_a, g_coeff_b, g_coeff_c);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
        {
            // Перерисовка при изменении размера окна
            if (g_parabola_ready || g_point_count > 0)
            {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_ERASEBKGND:
        {
            // Рисуем фон в WM_ERASEBKGND — меньше мерцания
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            return 1;
        }

        case WM_LBUTTONDOWN:
        {
            if (g_point_count < 3)
            {
                g_points[g_point_count].x = LOWORD(lParam);
                g_points[g_point_count].y = HIWORD(lParam);
                g_point_count++;

                // После третьего клика вычисляем параболу
                if (g_point_count == 3)
                {
                    g_parabola_ready = SolveParabola(g_points,
                                                      g_coeff_a,
                                                      g_coeff_b,
                                                      g_coeff_c);
                    if (!g_parabola_ready)
                    {
                        MessageBoxW(hwnd,
                            LangStr(L"IDS_COLLINEAR_POINTS"),
                            LangStr(L"IDS_ERROR_TITLE"),
                            MB_OK | MB_ICONWARNING);
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case ID_FILE_SAVE:
                    SaveBmpDialog(hwnd);
                    break;

                case ID_FILE_EXIT:
                    DestroyWindow(hwnd);
                    break;

                case ID_NEW_TASK:
                    // Сброс всего состояния
                    g_point_count = 0;
                    g_parabola_ready = false;
                    g_coeff_a = g_coeff_b = g_coeff_c = 0.0;
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;

                case ID_ABOUT:
                    MessageBoxW(hwnd,
                        LangStr(L"IDS_ABOUT_TEXT"),
                        LangStr(L"IDS_ABOUT_TITLE"),
                        MB_OK | MB_ICONINFORMATION);
                    break;
            }
            return 0;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // DPI awareness per monitor — ДО создания окна
    // Fallback для старых Windows (до Win10 1703)
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
    {
        // Игнорируем ошибку — работаем без DPI awareness
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    }

    const wchar_t CLASS_NAME[] = L"PictureWindowClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClassExW(&wc))
    {
        MessageBoxW(NULL, LangStr(L"IDS_CLASS_REGISTER_ERROR"), LangStr(L"IDS_ERROR_TITLE"), MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        LangStr(L"IDS_WINDOW_TITLE"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL)
    {
        MessageBoxW(NULL, LangStr(L"IDS_WINDOW_CREATE_ERROR"), LangStr(L"IDS_ERROR_TITLE"), MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
