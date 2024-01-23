class Widget:
    def __init__(self, var, type, config, obj=None):
        self.var = var
        self.type = type
        self.config = config
        self.obj = obj

    def get_obj(self):
        return self.obj or self.var
