class MotorCtrl
{

  public:
    MotorCtrl(int pwm_pin, int ls_pin, int rs_pin);
    ~MotorCtrl();

    void  drive(int spd_dir);
    void  stop();
    void  forward();
    void  backward();
    void  setSpeed(int val);
    bool  isForward();
    bool  isBackward();
    bool  isRunning();
    int   getSpeed();
    int   getDirection();
    void dumpState();
    
  private:
    int pwm_pin;
    int ls_pin;
    int rs_pin;
    int dir;
    int spd;
    float duty;
};
