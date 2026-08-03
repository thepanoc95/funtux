#include <Xm/Xm.h>
#include <Xm/PushB.h>

int main(int argc, char *argv[]) {
	    XtAppContext app;
	        Widget top_level, button;

		    top_level = XtVaAppInitialize(&app, "XApplication", NULL, 0, &argc, argv, NULL, NULL);

		        button = XmCreatePushButton(top_level, "Close Window", NULL, 0);
			    XtManageChild(button);

			        XtRealizeWidget(top_level);
				    XtAppMainLoop(app);

				        return 0;
}

