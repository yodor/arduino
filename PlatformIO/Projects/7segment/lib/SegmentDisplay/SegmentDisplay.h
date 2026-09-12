#ifndef SEGMENT_DISPLAY_H
#define SEGMENT_DISPLAY_H

typedef struct SegmentStatus {
  int value = 0;
  bool dot = false;
} SegmentStatus;


class SegmentDisplay {
    public:
        SegmentDisplay();
        ~SegmentDisplay();

        void setValue(float number);
        void refresh();
        void off();
        void on();

    protected:
        SegmentStatus cell[3];
        float currentValue;
        bool running;

        void update(int seg_num, int dgt, bool dot);

};

#endif