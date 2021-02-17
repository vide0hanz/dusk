int
width_wintitle_floating(Bar *bar, BarArg *a)
{
	if (!bar->mon->selws)
		return 0;
	return a->w;
}

int
draw_wintitle_floating(Bar *bar, BarArg *a)
{
	if (!bar->mon->selws)
		return 0;
	drw_rect(drw, a->x, a->y, a->w, a->h, 1, 1);
	return calc_wintitle_floating(bar->mon->selws, a->x, a->w, -1, flextitledraw, NULL, a);
}

int
click_wintitle_floating(Bar *bar, Arg *arg, BarArg *a)
{
	if (!bar->mon->selws)
		return 0;
	calc_wintitle_floating(bar->mon->selws, 0, a->w, a->x, flextitleclick, arg, a);
	return ClkWinTitle;
}

int
calc_wintitle_floating(
	Workspace *ws, int offx, int tabw, int passx,
	void(*tabfn)(Workspace *, Client *, int, int, int, int, Arg *arg, BarArg *barg),
	Arg *arg, BarArg *barg
) {
	Client *c;
	int clientsnfloating = 0, w, r;
	int groupactive = GRP_FLOAT;

	for (c = ws->clients; c; c = c->next) {
		if (!ISVISIBLE(c))
			continue;
		if (ISFLOATING(c))
			clientsnfloating++;
	}

	if (!clientsnfloating)
		return 0;

	w = tabw / clientsnfloating;
	r = tabw % clientsnfloating;
	c = flextitledrawarea(ws, ws->clients, offx, r, w, clientsnfloating, SCHEMEFOR(GRP_FLOAT), 0, 0, 1, passx, tabfn, arg, barg);
	return 1;
}